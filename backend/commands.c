// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	commands.c

	1.2 - adjust ws magic number
	1.3 - adjust ws magic number read...
	1.4 - adjust ws magic number, 64-bit clean fmt
*/

#define COMMANDS_C 1

#include <sys/stat.h>	/* stat()/S_ISDIR -- portable "does this directory
			   exist" check for do_calldir(); already relied on
			   as part of the portable baseline by both platform
			   files (linux_platform.c calls it directly, and
			   windows_platform.c already includes this header) */

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"
#include "fkeys.h"
#include "externs.h"
#include "platform.h"

void print_wsid(void);	/* below, after char_flip() (V108) */

#define LTABLE_SIZE 4000L

/* SYMT_REL removed -- SYMT is now embedded in workspace, all me_reference
   values are plain ws_off_t offsets, no region tagging needed. */

extern UCHAR *all_fnames[];

extern long wssize;
extern UCHAR var_lo,var_hi,alph_lo,alph_hi;
/* Resolved workspace/function directory via $APLHOME */
extern void get_aplhome_dir(char *out, size_t outsz, const char *subdir);
extern int step_mode;	/* exec.c -- )STEP */
int tablecase = 1;

/*

extern UCHAR op_table[];

extern long file_serial();
extern long GarbCollect();
extern UCHAR *get_vstr();
extern short lookup_sym();
extern short compline();
extern short pop_fn();
extern short vout();
extern struct ValEnt **get_vparm();
extern void message(short n);
extern void Adjust_Stack();
extern void get_mplenv(char *s);
*/


/*
extern UCHAR *all_fnames[];
extern int term_fd;
*/

FILE *dbgf = 0L;
int text_dump_flag = 0;


UCHAR cmd_table[] = {
	'O','F','F',0,		/* 0 - )off */
	'V','A','R',0,		/* 1 - )vars */
	'D','E','B','U','G',0,	/* 2 - )debug */
	'D','U','M','P',0,	/* 3 - )dump */
	'C','L',0,		/* 4 - )clear */
	'W','S',0,		/* 5 - )wsid */
	'G','A','R','B',0,	/* 6 - )garb */
	'S','A','V','E',0,	/* 7 - )save */
	'L','O','A','D',0,	/* 8 - )load */
	'E','R','A',0,	 	/* 9 - )erase */
	'F','N','S','L',0,	/* 10 - )fnsl */
	'F','N','S',0,		/* 11 - )fns */
	'S','Y','M',0, 		/* 12 - )symbols */
	'M','E','M',0,		/* 13 - )mem */
	'O','R',0,		/* 14 - )origin */
	'W','I',0,		/* 15 - )width */
	'E','N','T',0,		/* 16 - )enter (fn) */
	'E','D',0,		/* 17 - )edit */
	'S','I','V',0,		/* 18 - )siv */
	'L','I','B',0,		/* 19 - )lib */
	'P','U','R',0,		/* 20 - )purge */
	'S','Y','S',0,		/* 21 - )sys -- removed V81 (Incorrect Command);
				   the name keeps the slot, see do_command() */
	'S','I',0,		/* 22 - )si */
	'S','T','A',0,		/* 23 - )stack */
	'L','I',0,		/* 24 - )list */
	'O','U','T',0,		/* 25 - )outsync */
	'H','E',0,		/* 26 - )help -- removed V81 */
	'B','O',0,		/* 27 - )boot */
	'P','S',0,		/* 28 - )psnap */
	'B','A',0,		/* 29 - )base */
	'S','T','O','P',0,	/* 30 - )stop (was )ltrace; same slot, so the
				   numbering after it is unchanged) */
	'T','R',0,		/* 31 - )trace */
	'R','I',0,		/* 32 - )right */
	'L','E',0,		/* 33 - )left */
	'S','C','R',0,		/* 34 - )scrh -- removed V81 */
	'H','I','S','T',0,	/* 35 - )hist -- not implemented */
	'C','A','S','E',0,	/* 36 - )case -- removed V81 */
	'V','E','R',0,		/* 37 - )version -- removed V81 */
	'T','E','X','T',0,	/* 38 - )text -- removed V102 */
	'D','R','O','P',0,	/* 39 - )drop */
	'R','E','S',0,		/* 40 - )resize */
	'D','I','G',0,		/* 41 - )digits */
	'I','N','T','D','I','V',0,	/* 42 - )intdiv */
	'F','O','N','T',0,	/* 43 - )font -- no entry above starts with "FO",
				   so nothing earlier in the table captures it */
	'S','T','E','P',0,	/* 44 - )step -- STA ()stack) and STOP come
				   earlier but neither is a prefix of STEP */
	'I','M','A','G',0,	/* 45 - )imag -- so )IMAGE and )IMAGES work
				   too; INTDIV is the only other I entry */
	'F','S','E','A','R','C','H',0,	/* 46 - )fsearch (V77) -- no entry
				   above is a prefix of it (FNSL, FNS and
				   FONT part at the second letter), and it
				   is typed in full: no short form */
	'M','I','D','I',0,	/* 47 - )midi -- MEM is the only other M
				   entry and isn't a prefix of it; typed in
				   full (no short form), so )MIDIS works too */
	'S','E','Q','U','N','I','T',0,	/* 48 - )sequnit (V102) -- none of
				   the S entries above (SAVE, SYM, SYS, SIV,
				   SI, STA, STOP, SCR, STEP) is a prefix of
				   it; typed in full, no short form */
	'@',0,	/* end of table -- do_command() and the command list stop here */
	0	/* empty name: table_flip() (set_case) stops here; without it,
		   it read, and could case-flip, the byte past the table */
};

void
dbget()
{
char path[PATHBUF_SIZE];

	if (!dbgf) 
	{
		get_aplhome_dir(path, sizeof(path), "");
		strcat(path,"debug.log");
		dbgf = fopen(path,"a+");

		if (dbgf)
		{
			PRINT("Writing to debug.log at: ");
			PRINT(path);
			NL();
		}
		
	}
}

void
CNL()
{
	switch (SILENT_COMMAND) 
	{
		case 0:
			NL();
			break;
		case 2:
			dbget();
			fprintf(dbgf,"\n");
			fflush(dbgf);
			break;
	}
}

void
CPRINT(s)
char *s;
{
	switch (SILENT_COMMAND) 
	{
		case 0:
			PRINT(s);
			break;
		case 2:
			dbget();
			fprintf(dbgf,"%s",s);
			fflush(dbgf);
			break;
	}
}

void
Cmessage(n)
short n;
{
	if (!SILENT_COMMAND) message(n);
}

void
stamp_ws()
{
short i;
int32_t *lp;

    get_tod();
    lp = (int32_t *)LSCRATCH;
    for (i=0;i<6;i++) wspace->tstamp[i] = (short)lp[i];
}

void
print_stamp()
{
char *cp;	
short *sp;

	if (SILENT_COMMAND) return;	
	cp = (char *)LSCRATCH;
	sp = wspace->tstamp;
	sprintf(cp," %d.%02d.%02d %02d/%02d/%02d ",sp[3],sp[4],sp[5],sp[1],sp[2],sp[0]%100);
	PRINT(cp);
}

void
print_num(l)
long l;
{
char cb[15];
	if (SILENT_COMMAND) return;
	sprintf(cb,"%ld",l);
	PRINT(cb);
}

short
print_name(i)
short i;
{
struct NameEnt *ne;
UCHAR *cp;
REG short j;
static UCHAR cb[256];

	cb[1] = '\0';
	if (SYMT[i].namelen) 
	{
		if (SYMT[i].namelen <= SYMNAME_MAX) 
		{
			cp = (UCHAR *)&SYMT[i].name.c[0];			
		} else {
			ne = (struct NameEnt *)WS_PTR(SYMT[i].name.a);
			cp = ne->ne_data;
		}
		i = SYMT[i].namelen;
		for (j=0;j<i;j++) 
		{
			if (symbolcase) cb[0] = char_flip(*cp++);
			else cb[0] = *cp++;
			CPRINT(cb);
		}
	} else {
		CPRINT("********");
		i = 8;
	}
	CURSX += i;
	return(i);
}

void
go_lower(cp)
char *cp;
{
	while (*cp) 
	{
		if (*cp >= 'A' && *cp <= 'Z') *cp += 32;
		cp++;
	}
}

/* ∆ in a function's .fn file name (V107). ∆ (CHAR_DELTA, font code
   0xC8) isn't a character a file name can carry the same way on
   Linux (not UTF-8) and Windows (the ANSI code page), so on disk it is
   '='. No name can contain '=', so the mapping is one to one; and '-'
   was not used, as )FNSL's filter reads X-Y as a range. Every name
   without ∆ gives the same file name as before. The front end does the
   same (aplFnFileStem() in APLTerminalWidget.cpp). */
#define FN_DELTA_CHAR '='

/* $APLHOME/fns/NAME.fn for a function name: A-Z lowered as go_lower()
   does, ∆ as FN_DELTA_CHAR. out has LINE_SIZE bytes. name is left as
   it was. Used by )ENTER, )DROP and )LIST. */
static void
fn_file_path(char *out, const char *name)
{
char *cp;
UCHAR c;

	get_aplhome_dir(out, LINE_SIZE, "fns");
	cp = out + strlen(out);
	while (*name && cp < out + LINE_SIZE - 4) {
		c = (UCHAR)*name++;
		if (c >= 'A' && c <= 'Z') c += 32;
		else if (c == CHAR_DELTA) c = FN_DELTA_CHAR;
		*cp++ = (char)c;
	}
	strcpy(cp, ".fn");
}

/* A .fn file name's character as the function name has it: the
   reverse of fn_file_path()'s ∆ mapping, for )FNSL and )FSEARCH */
static UCHAR
fn_shown_char(UCHAR c)
{
	return (c == FN_DELTA_CHAR) ? (UCHAR)CHAR_DELTA : c;
}

short
on_stack(f)
short f;
{
int32_t *sp,t;
short fline;

	if (FUNC_NUM == f) return(1);
	Adjust_Stack();
	sp = MSTACK;
	while (sp < STACKMAX) {
		sp++;	/* skip over emark */
		fline = (short)((*sp++ >> FLINE_SHIFT) & FLINE_MASK);
		t = *sp++;
		if (fline) {
			fline = (short)((t >> FNUM_SHIFT) & FNUM_MASK);
			if (fline == f) return(1);
		}
		while (*sp != STACK_EMARK) {
			if (*sp == STACK_TEMP_VAL) sp++;
			sp++;
		}
	}
	return(0);
}

static void get_sym_name_str(short idx, char *buf, int buflen)
{
    struct NameEnt *ne;
    UCHAR *cp;
    int len = SYMT[idx].namelen;
    if (len <= 0) { buf[0] = '\0'; return; }
    if (len <= SYMNAME_MAX)
        cp = (UCHAR *)&SYMT[idx].name.c[0];
    else {
        ne = (struct NameEnt *)WS_PTR(SYMT[idx].name.a);
        cp = ne->ne_data;
    }
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, cp, len);
    buf[len] = '\0';
}

static int sym_name_cmp(const void *a, const void *b)
{
    char na[256], nb[256];
    get_sym_name_str(*(short *)a, na, sizeof(na));
    get_sym_name_str(*(short *)b, nb, sizeof(nb));
    return strcmp(na, nb);
}

/* ============================================================
   Optional name filter for )VARS, )FNS, and )SYMS
   ============================================================
   Grammar applied to the text following the command name:
     *        matches zero or more characters
     X-Y      a single character, a dash, and a single character
              matches one character in [X,Y], case-insensitively.
              Any other placement of '-' (nothing usable before
              it, or its bounds already consumed by an earlier
              range) is treated as a literal '-' rather than a
              syntax error.
     other    matches itself, case-insensitively

   Convenience: unless the argument already ends in '*', one is
   appended before matching -- so "L" behaves like "L*" (filter
   by first letter) and "*TEST" behaves like "*TEST*" (filter by
   substring). "A-D*TEST" behaves like "[A-D]*TEST*".
   ============================================================ */

#define FILT_PAT_SIZE 64

static int
filt_up(c)
int c;
{
	if (c >= 'a' && c <= 'z') c -= 32;
	return(c);
}

static int
filt_match(pat,str)
const char *pat,*str;
{
int lo,hi,c;

	while (*pat) {
		if (*pat == '*') {
			while (*pat == '*') pat++;
			if (!*pat) return(1);
			while (*str) {
				if (filt_match(pat,str)) return(1);
				str++;
			}
			return(0);
		}

		/* X-Y : single char, dash, single char -> range */
		if (pat[1] == '-' && pat[2] && pat[2] != '*') {
			lo = filt_up((int)(UCHAR)pat[0]);
			hi = filt_up((int)(UCHAR)pat[2]);
			if (lo > hi) { c = lo; lo = hi; hi = c; }
			if (!*str) return(0);
			c = filt_up((int)(UCHAR)*str);
			if (c < lo || c > hi) return(0);
			pat += 3;
			str += 1;
			continue;
		}

		if (!*str) return(0);
		if (filt_up((int)(UCHAR)*pat) != filt_up((int)(UCHAR)*str)) return(0);
		pat++;
		str++;
	}
	return(*str == '\0');
}

/* Normalize a raw command argument into a filter pattern: take the
   first whitespace-delimited token, then append a trailing '*'
   unless one is already there.

   A typed '*' arrives here as CHAR_POW, not literal 0x2a -- itrans[]
   (platform_translate.c) maps keyboard '*' to the internal power/
   exponent code, not to itself. Letters and '-' are unaffected by
   this (letters just get case-flipped, which filt_up() already
   ignores; '-' is identity in itrans[]), but '*' needs an explicit
   translation back to the literal wildcard byte this parser expects. */
static void
filt_build_pattern(raw,out,outsz)
const char *raw;
char *out;
int outsz;
{
int len;

	if (!raw) raw = "";
	while (*raw == ' ') raw++;

	len = 0;
	while (raw[len] && raw[len] != ' ' && len < outsz - 2) {
		out[len] = ((UCHAR)raw[len] == CHAR_POW) ? '*' : raw[len];
		len++;
	}
	out[len] = '\0';

	if (len == 0 || out[len-1] != '*') {
		out[len] = '*';
		out[len+1] = '\0';
	}
}

/* ============================================================
   do_calldir() - shared implementation behind )LIB and )FNSL.

   Session 2 platform dedup (see PORTING_PLAN.md): calldir() was
   retyped near-verbatim in linux_platform.c and windows_platform.c.
   Only raw directory enumeration was genuinely platform-specific,
   and get_first_dir()/get_next_dir()/cleanup_dir() already existed
   in identical shape on both sides (filfuncs.c's f_fdir(), for
   ⎕FDIR, is their only other caller, and it's confirmed safe to
   share -- see the porting plan). This walks that shared iterator
   instead of calling opendir()/readdir() or FindFirstFileA()/
   FindNextFileA() directly, and matches names with the same
   filt_match() grammar )VARS/)FNS/)SYMS already use, instead of
   fnmatch() (Linux) or FindFirstFileA()'s native wildcard dialect
   (Windows).

   Two behavior changes fall out of that, both intentional:

   1. Matching is now case-insensitive on both platforms. Linux's
      fnmatch() was case-sensitive (no FNM_CASEFOLD) while Windows's
      FindFirstFileA() was case-insensitive (NTFS default) -- exactly
      the divergence the porting plan flagged. filt_match() folds
      case on both sides, so behavior now matches what Windows
      already did.

   2. Subdirectories are now excluded from the listing on both
      platforms. Windows's calldir() already skipped them via
      FILE_ATTRIBUTE_DIRECTORY; Linux's calldir() did not -- every
      readdir() entry went straight into the list with no S_ISDIR
      check, so e.g. )LIB in a directory containing a subfolder
      would have printed the subfolder's name as if it were a
      workspace. get_first_dir()/get_next_dir() already tag every
      entry 'D' or 'F' for exactly this purpose; do_calldir() uses
      that tag to drop directories, matching Windows's (and the
      obviously intended) prior behavior.

   One deliberate non-change: get_first_dir()/get_next_dir() can't
   tell "no such directory" from "directory exists but is empty" --
   both just stop producing entries. Rather than let that collapse
   the old "Can't find: X" message into firing on legitimately empty
   directories, this stat()s dirbuf up front (portable -- both
   platform files already build against sys/stat.h) and only reports
   "Can't find" when dirbuf isn't a directory at all.

   NOTE: filt_match() has no '?' single-char wildcard, unlike
   fnmatch()/FindFirstFileA(). Not believed to matter in practice --
   '*' is the only wildcard documented anywhere for )LIB/)FNSL -- but
   flagging it here in case that assumption is wrong.

   For )FNSL (V107), calldir_fns is set around the call: each file
   name's FN_DELTA_CHAR ('=') is shown as ∆ again, and the pattern is
   matched against that shown name, so )FNSL A∆ finds A∆B (file
   a=b.fn) and a '-' in the pattern is still only ever a range.
   ============================================================ */
static int calldir_fns = 0;

void
do_calldir(s)
char *s;
{
struct stat st;
char dirbuf[PATHBUF_SIZE];
char pattern[PATHBUF_SIZE];
char (*names)[64];
char *entry, *last_slash, *cp;
int count, capacity, k;

	if (DEBUGFLAG) { NL(); PRINT("["); PRINT(s ? s : ""); PRINT("]"); NL(); }

	if (!s || !*s) {
		strcpy(dirbuf, ".");
		strcpy(pattern, "*.ws");
	} else {
		last_slash = strrchr(s, '/');
		if (!last_slash) last_slash = strrchr(s, '\\');
		if (!last_slash) {
			strcpy(dirbuf, ".");
			strncpy(pattern, s, sizeof(pattern) - 1);
			pattern[sizeof(pattern) - 1] = '\0';
		} else if (last_slash == s) {
			strcpy(dirbuf, "/");
			strncpy(pattern, last_slash + 1, sizeof(pattern) - 1);
			pattern[sizeof(pattern) - 1] = '\0';
		} else {
			size_t dlen = (size_t)(last_slash - s);
			if (dlen >= sizeof(dirbuf)) dlen = sizeof(dirbuf) - 1;
			strncpy(dirbuf, s, dlen);
			dirbuf[dlen] = '\0';
			strncpy(pattern, last_slash + 1, sizeof(pattern) - 1);
			pattern[sizeof(pattern) - 1] = '\0';
		}
		if (!*pattern) strcpy(pattern, "*");
	}

	if (DEBUGFLAG) { PRINT(dirbuf); NL(); }

	if (stat(dirbuf, &st) != 0 || !S_ISDIR(st.st_mode)) {
		PRINT("Can't find: ");
		PRINT(dirbuf);
		NL();
		return;
	}

	count = 0;
	capacity = 64;
	names = (char (*)[64])malloc((size_t)capacity * sizeof(*names));
	if (!names) return;

	for (entry = get_first_dir(dirbuf); entry; entry = get_next_dir()) {
		char type = entry[0];
		char *nm = entry + 1;
		char shown[PATHBUF_SIZE];
		int nl, stem, i;

		if (type != 'F') continue;		/* skip subdirectories */
		if (calldir_fns) {			/* '=' back to ∆ (V107) */
			for (i = 0; nm[i] && i < (int)sizeof(shown) - 1; i++)
				shown[i] = (char)fn_shown_char((UCHAR)nm[i]);
			shown[i] = '\0';
			nm = shown;
		}
		if (!filt_match(pattern, nm)) continue;

		nl = strlen(nm);
		stem = nl;
		for (i = nl - 1; i > 0; i--)
			if (nm[i] == '.') { stem = i; break; }

		if (count >= capacity) {
			char (*tmp)[64];
			capacity *= 2;
			tmp = (char (*)[64])realloc(names, (size_t)capacity * sizeof(*names));
			if (!tmp) break;
			names = tmp;
		}

		if (stem >= 63) stem = 63;
		/* stem <= strlen(nm), so a plain copy of stem bytes; memcpy,
		   not strncpy, which -Wstringop-truncation flags once nm can
		   be the shown[] buffer (V107) */
		memcpy(names[count], nm, (size_t)stem);
		names[count][stem] = '\0';
		for (cp = names[count]; *cp; cp++)
			if (*cp >= 'a' && *cp <= 'z') *cp -= 32;
		count++;
	}
	cleanup_dir();

	qsort(names, (size_t)count, sizeof(*names),
	      (int(*)(const void *, const void *))strcmp);

	for (k = 0; k < count; k++) {
		char buf[64];
		strncpy(buf, names[k], 20);
		buf[20] = '\0';
		strcat(buf, "                    ");
		buf[20] = '\0';
		if ((CURSX + 20) > WIDTH) NL();
		PRINT(buf);
	}

	free(names);
	NL();
}

/* ============================================================
   print_name_columns() - lay out a list of names across )WIDTH.
   Shared by )FONT and )IMAG (it was do_fonts()'s own loop until
   )IMAG needed the same layout).

   Columns are sized to the longest name being shown plus two
   spaces -- font families and file names often contain single
   spaces, and a fixed 10- or 20-wide column (what )FNS/)LIB use)
   would either truncate them or make it unclear where one name
   ends and the next begins. A name wider than the whole line
   still prints, alone on its line. No trailing padding after the
   last name on a line, so a copied name doesn't pick up stray
   spaces. Names are printed as-is, without OUT_TRANS_FLAG.
   ============================================================ */
static void
print_name_columns(names,n)
char **names;
long n;
{
long k;
int len, maxlen, colw, col, pos;

	maxlen = 0;
	for (k = 0; k < n; k++) {
		len = (int)strlen(names[k]);
		if (len > maxlen) maxlen = len;
	}

	colw = maxlen + 2;
	col = 0;	/* start column of the name just printed */
	pos = 0;	/* current output column */
	for (k = 0; k < n; k++) {
		if (pos) {
			if (col + colw + maxlen > WIDTH - 1) {
				NL();
				col = pos = 0;
			} else {
				col += colw;
				while (pos < col) { PRINT(" "); pos++; }
			}
		}
		PRINT(names[k]);
		pos += (int)strlen(names[k]);
	}
	if (pos) NL();
}

/* ============================================================
   do_fonts() - )FONT and )FONT pattern.
   Lists the font families available to graphics windows (for
   WFONT's face selection), in Qt's own capitalization so a name
   can be copied straight into a string -- family matching ignores
   case, so a name typed in uppercase works too.

   Pattern: same filt_build_pattern()/filt_match() grammar )FNS/
   )VARS/)LIB use (leading letters, '*', X-Y ranges, case
   ignored). Only the first word is used, so a name with a space
   in it is matched with '*' instead: )FONT DEJAVU*MONO.

   Sorted ignoring case, laid out by print_name_columns() above.

   Font names come from the OS, so they're plain ASCII printed
   without OUT_TRANS_FLAG -- the same way do_calldir() prints file
   names.

   FONT_LIST_BUF is a generous fixed ceiling, not a measured need:
   a desktop system with a few hundred families is ~10 KB of
   names. sys_font_families() stops at whole names, so an
   oversize list is cut short rather than corrupted.
   ============================================================ */
#define FONT_LIST_BUF 65536L

/* Case-ignoring name order, exact order as the tie-break. Named for
   )FONT, where it started; )IMAG sorts with it too. */
static int
font_name_cmp(a,b)
const void *a,*b;
{
const char *s = *(const char * const *)a;
const char *t = *(const char * const *)b;
int c1,c2;

	for (;;) {
		c1 = filt_up((int)(UCHAR)*s);
		c2 = filt_up((int)(UCHAR)*t);
		if (c1 != c2 || !c1) break;
		s++;
		t++;
	}
	if (c1 != c2) return(c1 - c2);
	/* equal ignoring case: fall back to exact order so the
	   result doesn't depend on qsort()'s tie handling */
	return(strcmp(*(const char * const *)a, *(const char * const *)b));
}

short
do_fonts(pattern)
UCHAR *pattern;
{
char *buf, **names, *cp;
char pbuf[FILT_PAT_SIZE];
long count, n, k;
int len, filtering;

	buf = (char *)malloc((size_t)FONT_LIST_BUF);
	if (!buf) return(SET_ERR(ERR_WSFULL));
	count = sys_font_families(buf, FONT_LIST_BUF);
	if (count < 0) {
		free(buf);
		PRINT("Font list not available");
		NL();
		return(0);
	}

	filtering = 0;
	if (pattern && *pattern) {
		filt_build_pattern((const char *)pattern, pbuf, sizeof(pbuf));
		filtering = 1;
	}

	names = (char **)malloc((size_t)(count ? count : 1) * sizeof(char *));
	if (!names) {
		free(buf);
		return(SET_ERR(ERR_WSFULL));
	}

	n = 0;
	cp = buf;
	for (k = 0; k < count; k++) {
		len = (int)strlen(cp);
		if (!filtering || filt_match(pbuf, cp))
			names[n++] = cp;
		cp += len + 1;
	}

	qsort(names, (size_t)n, sizeof(char *), font_name_cmp);
	print_name_columns(names, n);

	free(names);
	free(buf);
	return(0);
}

/* ============================================================
   list_home_folder() - )IMAG and )MIDI (do_images() and
   do_midi(), after it): the argument, then the $APLHOME folder
   ("images", "midi") to list when it isn't an absolute path.
   Written for )IMAG; )MIDI (V98) shares it unchanged, so the
   img_ helpers below keep their names and everything here that
   says images/ applies to midi/ the same way.

   Lists what's in $APLHOME/images/ (or a folder under it, or any
   absolute folder) exactly as the file system spells it -- no
   case change, extensions kept -- so a name can be copied straight
   into a string for WILOAD/WIRELOAD/WISAVE (MIDIREAD/MIDIINFO/
   MIDIWRITE for )MIDI).

   What's listed: every file, plus subfolders shown as "name/"
   (not descended into). Names starting with '.' are hidden;
   get_first_dir()/get_next_dir() already skip them on both
   platforms. Sorted ignoring case (font_name_cmp), laid out by
   print_name_columns(). Nothing matching prints nothing, as for
   )FONT.

   The argument, only its first word unless quoted:
     (none)          $APLHOME/images/
     pattern         names there matching pattern
     sub/            what's in images/sub/  (the trailing separator
                     is what says "folder"; plain "sub" is a
                     pattern, matching sub/ along with subway.png)
     sub/pattern     names in images/sub/ matching pattern
     /abs/dir/...    an absolute folder, by the same test WILOAD
                     uses (grfuncs.c's is_absolute_path(): leading
                     '/' or a drive letter "X:"). No '~' expansion,
                     since WILOAD doesn't do one either.
   Separators are '/' or '\\', as do_calldir() accepts. ".." works;
   nothing restricts the listing to images/, since WILOAD itself
   takes absolute paths.

   Pattern grammar is filt_match()'s ('*', X-Y ranges, case
   ignored, a trailing '*' implied), applied to the last part only.

   HOW THE ARGUMENT ARRIVES (checked by running apl_cli, which feeds
   the backend through the same translation as a GUI paste): outside
   quotes, input translation (itrans[]) has changed some characters
   by the time COMMANDP sees them --
       letters     case swapped (Sub -> sUB)
       /           '%'       (so a real '%' can't be told from '/')
       *           CHAR_POW
       ~           '|'
       =           CHAR_EQ
       ^  and  |  '&' and '!' (colliding with the real '&' and '!')
   -- while digits, '.', '_', '-', '\\' and most other punctuation
   arrive as typed. img_get_arg() undoes the first five; '^' and '|'
   are left alone since they can't be told apart and are unusual in
   file names. Inside quotes, nothing is translated at all, so a
   quoted argument arrives exactly as typed: that's the way to give
   a name with a space, '%', '^' or '|' in it -- )IMAG 'My Pics/'.
   '' inside quotes is one quote, as in an APL string; a missing
   closing quote takes the rest of the line.

   This un-translation is a new pattern in this file -- other
   commands either don't care (filt_match() ignores case) or fold
   case themselves (do_clist()'s go_lower()). It's needed here
   because Linux file names are case-sensitive.

   Folder case: typed text may still not have the disk's case (the
   GUI keyboard may give only one case of letters), so each folder
   in the path is tried exactly first, then matched ignoring case
   against the folders actually there. One case-ignoring match is
   used; none is "Can't find", two or more (Sub/ and SUB/) is
   "Ambiguous" -- quote the path to pick one exactly. This applies
   to quoted arguments too; it only comes into play when the exact
   name doesn't exist.

   Errors print a message and return 0, like do_calldir()'s
   "Can't find". The folder in the message is shown as typed.
   ============================================================ */

static int
img_is_sep(c)
int c;
{
	return(c == '/' || c == '\\');
}

/* Same test as grfuncs.c's is_absolute_path() (static there, so
   repeated rather than shared): leading '/', or a drive letter. */
static int
img_is_absolute(p)
const char *p;
{
int c;

	if (!p || !*p) return(0);
	if (p[0] == '/') return(1);
	c = (UCHAR)p[0] | 0x20;
	if (c >= 'a' && c <= 'z' && p[1] == ':') return(1);
	return(0);
}

/* The argument, as a plain file-system string (see above for what
   is undone and why). -1 if it doesn't fit in outsz. */
static int
img_get_arg(raw,out,outsz)
const UCHAR *raw;
char *out;
int outsz;
{
int n, c;

	n = 0;
	if (!raw) raw = (const UCHAR *)"";
	while (*raw == ' ') raw++;

	if (*raw == CHAR_QT) {
		raw++;
		while (*raw) {
			if (*raw == CHAR_QT) {
				if (raw[1] != CHAR_QT) break;	/* closing quote */
				raw++;				/* '' -> ' */
			}
			if (n >= outsz - 1) return(-1);
			out[n++] = (char)*raw++;
		}
		if (!*raw)	/* no closing quote: drop trailing blanks */
			while (n > 0 && out[n-1] == ' ') n--;
	} else {
		while (*raw && *raw != ' ') {
			c = *raw++;
			if (c >= 'a' && c <= 'z') c -= 32;
			else if (c >= 'A' && c <= 'Z') c += 32;
			else if (c == '%') c = '/';
			else if (c == CHAR_POW) c = '*';
			else if (c == '|') c = '~';
			else if (c == CHAR_EQ) c = '=';
			if (n >= outsz - 1) return(-1);
			out[n++] = (char)c;
		}
	}
	out[n] = '\0';
	return(0);
}

/* name equal to the len bytes at comp, ignoring case */
static int
img_same_nocase(name,comp,len)
const char *name,*comp;
int len;
{
int i;

	for (i = 0; i < len; i++)
		if (!name[i] || filt_up((int)(UCHAR)name[i]) !=
				filt_up((int)(UCHAR)comp[i]))
			return(0);
	return(name[len] == '\0');
}

/* get_first_dir() wants a folder without a trailing separator
   (Windows's appends "\\*" itself), except for a root: "/", "C:/" */
static char *
img_first_entry(dir)
const char *dir;
{
char buf[PATHBUF_SIZE];
int n;

	strncpy(buf, dir, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	n = (int)strlen(buf);
	if (n > 1 && img_is_sep(buf[n-1]) &&
	    !(n == 3 && buf[1] == ':'))
		buf[n-1] = '\0';
	return(get_first_dir(buf));
}

/* Append the folders in dirs[0..dlen) to out (which already holds
   the starting folder, ending in a separator, or is empty for a
   drive-relative "C:"), one at a time: exact name first, then a
   single case-ignoring match. 0 = found, 1 = not found (or too
   long), 2 = more than one case-ignoring match. */
static int
img_resolve_dir(out,outsz,dirs,dlen)
char *out;
int outsz;
const char *dirs;
int dlen;
{
struct stat st;
char trial[PATHBUF_SIZE], found[PATHBUF_SIZE];
const char *comp, *end;
char *entry;
int clen, olen, nfound;

	end = dirs + dlen;
	while (dirs < end) {
		while (dirs < end && img_is_sep((UCHAR)*dirs)) dirs++;
		if (dirs >= end) break;
		comp = dirs;
		while (dirs < end && !img_is_sep((UCHAR)*dirs)) dirs++;
		clen = (int)(dirs - comp);

		olen = (int)strlen(out);
		if (olen + clen + 2 > outsz || olen + clen + 2 > (int)sizeof(trial))
			return(1);
		memcpy(trial, out, (size_t)olen);
		memcpy(trial + olen, comp, (size_t)clen);
		trial[olen + clen] = '\0';

		if (stat(trial, &st) != 0 || !S_ISDIR(st.st_mode)) {
			/* not there as typed: look for it ignoring case */
			nfound = 0;
			for (entry = img_first_entry(out[0] ? out : ".");
			     entry; entry = get_next_dir()) {
				if (entry[0] != 'D') continue;
				if (!img_same_nocase(entry + 1, comp, clen)) continue;
				if (++nfound == 1) {
					strncpy(found, entry + 1, sizeof(found) - 1);
					found[sizeof(found) - 1] = '\0';
				}
			}
			cleanup_dir();
			if (nfound == 0) return(1);
			if (nfound > 1) return(2);
			clen = (int)strlen(found);
			if (olen + clen + 2 > outsz) return(1);
			memcpy(trial + olen, found, (size_t)clen + 1);
		}
		strcpy(out, trial);
		strcat(out, "/");
	}
	return(0);
}

static short
list_home_folder(arg,home)
UCHAR *arg;
const char *home;
{
char path[PATHBUF_SIZE], dir[PATHBUF_SIZE], pbuf[PATHBUF_SIZE];
char **names, **tmp, *entry, *pat;
int last, plen, i, rc, len;
long n, cap, k;

	if (img_get_arg(arg, path, sizeof(path)) < 0) {
		PRINT("Name too long");
		NL();
		return(0);
	}

	/* folder part is everything through the last separator */
	last = -1;
	for (i = 0; path[i]; i++)
		if (img_is_sep((UCHAR)path[i])) last = i;

	if (img_is_absolute(path)) {
		plen = (path[0] == '/') ? 1 : 2;
		if (plen == 2 && img_is_sep((UCHAR)path[2])) plen = 3;
		memcpy(dir, path, (size_t)plen);
		dir[plen] = '\0';
		if (last + 1 < plen) last = plen - 1;	/* "C:name" */
	} else {
		plen = 0;
		get_aplhome_dir(dir, sizeof(dir), home);
	}
	pat = path + last + 1;

	rc = 0;
	if (last >= plen)
		rc = img_resolve_dir(dir, (int)sizeof(dir), path + plen, last + 1 - plen);
	/* the starting folder itself is only checked here, e.g. "C:"
	   on a system with no such drive; like do_calldir(), an empty
	   folder is fine but a missing one is "Can't find" */
	if (!rc) {
		struct stat st;
		if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) rc = 1;
	}
	if (rc) {
		PRINT(rc == 2 ? "Ambiguous: " : "Can't find: ");
		*pat = '\0';	/* show just the folder part, as typed */
		PRINT(path);
		NL();
		return(0);
	}

	/* pattern: filt_match() grammar, trailing '*' implied. Built
	   here rather than by filt_build_pattern(), which stops at the
	   first blank -- a quoted name can have blanks in it. */
	if (*pat) {
		len = (int)strlen(pat);
		if (len > (int)sizeof(pbuf) - 2) len = (int)sizeof(pbuf) - 2;
		memcpy(pbuf, pat, (size_t)len);
		if (pbuf[len-1] != '*') pbuf[len++] = '*';
		pbuf[len] = '\0';
	}

	n = 0;
	cap = 64;
	names = (char **)malloc((size_t)cap * sizeof(char *));
	if (!names) return(SET_ERR(ERR_WSFULL));

	rc = 0;
	for (entry = img_first_entry(dir); entry; entry = get_next_dir()) {
		if (*pat && !filt_match(pbuf, entry + 1)) continue;
		if (n >= cap) {
			tmp = (char **)realloc(names, (size_t)cap * 2 * sizeof(char *));
			if (!tmp) { rc = 1; break; }
			names = tmp;
			cap *= 2;
		}
		len = (int)strlen(entry + 1);
		names[n] = (char *)malloc((size_t)len + 2);
		if (!names[n]) { rc = 1; break; }
		strcpy(names[n], entry + 1);
		if (entry[0] == 'D') strcat(names[n], "/");
		n++;
	}
	cleanup_dir();

	if (!rc) {
		qsort(names, (size_t)n, sizeof(char *), font_name_cmp);
		print_name_columns(names, n);
	}
	for (k = 0; k < n; k++) free(names[k]);
	free(names);
	return(rc ? SET_ERR(ERR_WSFULL) : 0);
}

/* )IMAG: $APLHOME/images/, where WILOAD looks */
short
do_images(arg)
UCHAR *arg;
{
	return(list_home_folder(arg, "images"));
}

/* )MIDI (V98): $APLHOME/midi/, where MIDIREAD, MIDIINFO and
   MIDIWRITE look. Every file is listed, as )IMAG lists every file
   (agreed V98), not just .mid ones. */
short
do_midi(arg)
UCHAR *arg;
{
	return(list_home_folder(arg, "midi"));
}

short
do_list(type,pattern)
short type;
char *pattern;
{
REG short i;
short w;
short *indices;
int nind = 0, k;
char pbuf[FILT_PAT_SIZE];
int filtering = 0;

    if (pattern && *pattern) {
        filt_build_pattern((const char *)pattern, pbuf, sizeof(pbuf));
        filtering = 1;
    }

    indices = (short *)malloc((size_t)SYMTACTUAL * sizeof(short));
    if (!indices) return(-1);

    for (i = 0; i < SYMTACTUAL; i++) {
        if (type == -1 || (SYMT[i].type == type && SYMT[i].value))
            if (SYMT[i].namelen) {
                if (filtering) {
                    char nbuf[256];
                    get_sym_name_str(i, nbuf, sizeof(nbuf));
                    if (!filt_match(pbuf, nbuf)) continue;
                }
                indices[nind++] = i;
            }
    }

    qsort(indices, (size_t)nind, sizeof(short), sym_name_cmp);

    w = 0;
    for (k = 0; k < nind; k++) {
        i = indices[k];
        if (!text_dump_flag) {
            w += 1 + SYMT[i].namelen;
            if ((w > (WIDTH - 1)) || (((w+10) > WIDTH) && type == -1)) {
                w = SYMT[i].namelen + 1;
                NL();
            }
        }
        if (text_dump_flag && SYMT[i].type == SYM_FUN) PRINT(")enter ");
        print_name(i);
        if (text_dump_flag) PRINT("\n");

        if (!text_dump_flag) {
            if (!SYMT[i].value) PRINT("-");
            else {
                if (WS_PTR(SYMT[i].value)->me_reference == WS_OFF(&SYMT[i].value)) PRINT(" ");
                else PRINT("#");
            }
        }

        if (!text_dump_flag) {
            while ((w % 10) && w < (WIDTH - 1)) { PRINT(" "); w++; }
        }

        if (!text_dump_flag && type == -1) {
            char cb[20];
            sprintf(cb, "%08x ", SYMT[i].value);
            PRINT(cb);
            w += 9;
        }
    }
    free(indices);
    if (!text_dump_flag) NL();
    return(0);
}

short
do_vars(pattern)
char *pattern;
{
	return(do_list(SYM_VAR, pattern));
}

short
do_fns(pattern)
char *pattern;
{
	return(do_list(SYM_FUN, pattern));
}

short
do_syms(pattern)
char *pattern;
{
	return(do_list(-1, pattern));
}

/*
	V113: helpers for )WSID, )SAVE and )DROP.

	file_exists(): a regular file is at path.

	ws_file_path(): $APLHOME/ws/NAME.ws, built exactly as do_save() and
	load_ws() build it -- the name as get_vstr() read it, not lowered --
	so a check here looks at the file )SAVE would write. out has
	LINE_SIZE bytes.

	shown_name(): a name as names are shown (print_wsid()'s flip, when
	symbolcase is set), with ∆ as the font byte (utf8 0, for terminal
	output) or as UTF-8 (utf8 1, for sys_confirm()'s dialog text, which
	the GUI shows in an ordinary font, not KAPL).
*/
static int
file_exists(const char *path)
{
struct stat st;

	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static void
ws_file_path(char *out, const char *name)
{
size_t n;

	get_aplhome_dir(out, LINE_SIZE, "ws");
	n = strlen(out);
	strncat(out, name, LINE_SIZE - n - 4);
	strcat(out, ".ws");
}

static void
shown_name(char *out, size_t size, const char *name, int utf8)
{
size_t n;
UCHAR c;

	n = 0;
	while (*name && n + 4 < size) {
		c = (UCHAR)*name++;
		if (c == CHAR_DELTA && utf8) {
			out[n++] = (char)0xE2;	/* U+2206 */
			out[n++] = (char)0x88;
			out[n++] = (char)0x86;
			continue;
		}
		if (symbolcase) c = char_flip(c);
		out[n++] = (char)c;
	}
	out[n] = '\0';
}

/*
	A different name than the active workspace's, whose .ws already
	exists (V113, the person): refused, so a later )SAVE can't overwrite
	that workspace without anyone noticing. Prints why and returns
	WSID_TAKEN; the name stays as it was. Only for a *different* name:
	)WSID or )SAVE with the active workspace's own name is the normal
	resave. The ∆ check comes first, so name has no ∆ here.
*/
#define WSID_TAKEN (-1)

static short
wsid_taken(const char *name, short saving)
{
char cb[WSID_SIZE + 8];
char nm[WSID_SIZE];
char path[LINE_SIZE];	/* not LINEA: COMMANDP points into it */

	/* the name as do_wsid() will store it: wsid keeps WSID_SIZE-1
	   characters, and that is the name )SAVE writes */
	strncpy(nm, name, WSID_SIZE - 1);
	nm[WSID_SIZE - 1] = '\0';

	if (!strcmp(nm, wspace->wsid)) return(0);
	ws_file_path(path, nm);
	if (!file_exists(path)) return(0);

	shown_name(cb, sizeof cb, nm, 0);
	CPRINT(saving ? "NOT SAVED, " : "NOT CHANGED, ");
	CPRINT(cb);
	CPRINT(" ALREADY EXISTS");
	CNL();
	CPRINT("TO REPLACE IT, DELETE IT FIRST WITH )DROP ");
	CPRINT(cb);
	CNL();
	return(WSID_TAKEN);
}

/*
	f 1: )WSID itself (shows the old name). f 0: do_save()'s rename,
	silent. Returns 0, ERR_COMMAND's code for a ∆ in the name, or
	WSID_TAKEN (V113) when the name belongs to another saved workspace
	-- already reported, so callers turn it into "no error", not into
	an error code (do_command() would read -1 as one).
*/
short
do_wsid(f)
short f;
{
	if (*COMMANDP) {
		COMMANDP = get_vstr(COMMANDP);
		/* ∆ is a name character since V107, but not yet in a
		   workspace name: it would go into the .ws file name as a
		   raw byte (see FN_DELTA_CHAR for function files). Until
		   workspace files get the same mapping, refused. Before
		   V107 the name silently stopped at the ∆. */
		if (COMMANDP && strchr((char *)VBUF, CHAR_DELTA))
			return(SET_ERR(ERR_COMMAND));
		if (COMMANDP && wsid_taken((char *)VBUF, (short)!f))
			return(WSID_TAKEN);
		if (COMMANDP) {
			if (f) {
				Cmessage(MSG_WAS);
				print_wsid();
				CNL();	/* V108: missing, so the next output
					   landed on the "Was" line */
			}
			strncpy((char *)wspace->wsid,(char *)VBUF,WSID_SIZE-1);
			wspace->wsid[WSID_SIZE-1] = '\0';
			return(0);
		}
	}
	if (f) {
		print_wsid();
		CNL();
	}
	return(0);
}

/*
	compiles LINEB into wspace entry
*/
struct FuncEnt *
MakeFNEnt()
{
short il,s;
long l;
struct FuncEnt *fe;
UCHAR c,*cp,*cpb;

	cp = LINEB;
	while (*cp == ' ' || *cp == 9) cp++;
	cpb = cp;
	while (*cpb) 
	{
		c = *cpb++;
		if (c == ':') 	/* found label, ignore */
		{
			cp = cpb;
			break;
		}
		if (c < '0' || c > '9') break;		/* no label, quit looking */
	}
	l = (long)sizeof(struct FuncEnt) - (long)sizeof(struct MemEnt);
	if (*cp == ')') 		/* a command */
	{
		il = 3 + strlen(cp);
		strcpy(&LINEA[2],cp);
		LINEA[0] = 255;
		LINEA[1] = 255;
	} else {
		s = compline(cp,LINEA);
		if (s) 
		{
			if (s == -9) ERROR_CODE = SET_ERR(ERR_BADSYS);	/* V109 */
			else if (s > -3) ERROR_CODE = SET_ERR(ERR_STOVF);
			else ERROR_CODE = SET_ERR(ERR_SYNTAX);
			return(0L);
		}
		il = (LINEA[0] << 8) + LINEA[1];
	}
	l += il - 4; /* first four characters are accounted for in struct */
	fe = (struct FuncEnt *)MakeEnt(l,ME_FUNCN,(ws_off_t *)0);
	if (!fe) 
	{
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return(0L);
	}
	memcpy((char *)fe->fe_exec,(char *)LINEA,(int)il);
	fe->fe_NextLine = 0;
	return(fe);
}

void
caseflip(cp)
UCHAR *cp;
{
int inq;
	inq = 0;

	while (*cp) 
	{
		if (*cp == '"') inq ^= 1;
		if (inq) cp++;
		else { *cp = char_flip(*cp); cp++; }
	}
}

void
sym_delete(i)
short i;
{
struct MemEnt *me;

/* 
	must add special memory deallocation here, and should do a nested
	delete of nested arrays.  Should split out the function so that
	non-symbol table entries can be processed as well.
*/

	me = SYMT[i].value ? WS_PTR(SYMT[i].value) : NULL;
	if (me) me_delete(me);

	SYMT[i].type = SYM_UNDEF;
	SYMT[i].value = 0;		/* mark deleted */
}

short
do_enter()
{
short i,line,loc,s,ms,rs,ls,zs,t,ct,qt;
short stovf;	/* a header name or local didn't fit in the symbol table */
long l;
FILE *ifp;
struct Func0Ent *f0e;
struct FuncEnt *fe;
ws_off_t *fep;
UCHAR c,cbuf[80];
UCHAR *ltable,*lp,*cp;
char *tp;

	ltable = (UCHAR *)malloc(LTABLE_SIZE); 
	if (!ltable) return(SET_ERR(ERR_NOMEM));
	/* tp holds a saved copy of the raw source line for error display.
	   Must NOT alias LSCRATCH -- MakeFNEnt() below recompiles the line
	   (via compline()/parsenums()), which writes numeric literals into
	   LSCRATCH and would clobber the saved text before it's printed. */
	tp = (char *)malloc(LINE_SIZE);
	if (!tp) {
		free(ltable);
		return(SET_ERR(ERR_NOMEM));
	}
	lp = ltable;

	strcpy(cbuf,COMMANDP);
	COMMANDP = cbuf;
	while (*COMMANDP) 
	{
		COMMANDP = get_vstr(COMMANDP);
		if (!COMMANDP) 	
		{
			i = SET_ERR(ERR_COMMAND);
			goto enterr;
		}

		fn_file_path((char *)LINEA, (char *)VBUF);
		ifp = fopen((char *)LINEA,"r");

		if (!ifp) 
		{
NL();
PRINT(LINEA);
NL();
			i = SET_ERR(ERR_FNOTF);
			goto enterr;
		}
/* first pass scan for labels */
		line = -1;
		while (fgets((char *)LINEA,LINE_SIZE,ifp)) 
		{
			{	/* strip line ending; checks length first, so a blank line
				   doesn't read (or zero) the byte before LINEA */
				size_t n = strlen((char *)LINEA);
				while (n > 0 && (LINEA[n-1] == 10 || LINEA[n-1] == 13)) LINEA[--n] = '\0';
			}
			ct = file_translate(LINEA,LINEB);
			strcpy(LINEA,LINEB);

			t = 0;
			while (LINEA[t] == ' ' || LINEA[t] == 9) t++;
			// if (ct) line++;	<-- removed, causes comment only lines not to be counted
			line++; /* every file line counts so comment only lines are counted too */
			i = t;
			while (line && LINEA[i]) 	/* don't proc line 0 */
			{
				c = LINEA[i++];
				if (c == ':') 
				{
					LINEA[i-1] = '\0';
					strcpy(lp,&LINEA[t]);
					lp += strlen(lp) + 1;
					*lp++ = (UCHAR)line >> 8;
					*lp++ = (UCHAR)(line & 255);
				}
				/* bail if not a name character. Until V107 this
				   tested letters and digits only, so a label with
				   '_' in it (MY_LOOP:) wasn't found. */
				if (!is_varch(c)) break;
			}
		}
		fseek(ifp,0L,0);	/* start again */
		*lp = 0;	/* terminate table */

/* second pass, compile */
		line = 0;
		ms = -1;	/* the function's symbol, set by line 0 (the header),
				   which always comes first. Set here only because
				   gcc 11 at -O1 (make check_san) can't see that and
				   warns at the error path's sym_delete(ms) (V107) */
		fep = NULL;	/* set while compiling line 0 (the header); still NULL
				   after the loop means the file was empty */
		while (fgets((char *)LINEA,LINE_SIZE,ifp)) 
		{
			{	/* strip line ending; checks length first, so a blank line
				   doesn't read (or zero) the byte before LINEA */
				size_t n = strlen((char *)LINEA);
				while (n > 0 && (LINEA[n-1] == 10 || LINEA[n-1] == 13)) LINEA[--n] = '\0';
			}
			strcpy(tp,LINEA);	/* make a copy for error msgs -- raw file bytes,
						   translated via orig_trans[] at display time to
						   match )LIST's handling in do_clist() */
			
			ct = file_translate(LINEA,LINEB);
			strcpy(LINEA,LINEB);

			if (line == 0) 
			{

				zs = ls = ms = rs = -1;
				stovf = 0;
				i = s = 0;
				while (i < ct) 	/* look for result var */
				{
					if (LINEA[i] == ';') break;
					if (LINEA[i] == CHAR_AS) 	/* assign? */
					{
						s = 1;
						break;
					}
					i++;
				}
				if (s) 
				{
					LINEA[i] = '\0';
					zs = lookup_sym(LINEA);
					if (zs < 0) stovf = 1;
					i++;
				} else i = 0;

				while (LINEA[i] == ' ') i++;
				s = i;

				while (is_varch(LINEA[i])) i++;

				ms = LINEA[i];
				LINEA[i] = '\0';
				ls = lookup_sym(&LINEA[s]);
				if (ls < 0) stovf = 1;
				LINEA[i] = (unsigned char)ms;
				while (LINEA[i] == ' ') i++;
				s = i;
				if (i < ct && LINEA[i] != ';') 
				{

					while (is_varch(LINEA[i])) i++;

					rs = LINEA[i];
					LINEA[i] = '\0';
					ms = lookup_sym(&LINEA[s]);
					if (ms < 0) stovf = 1;
					LINEA[i] = (unsigned char)rs;
					while (LINEA[i] == ' ') i++;
					s = i;
					if (LINEA[i] && LINEA[i] != ';') 
					{

						while (is_varch(LINEA[i])) i++;

						t = LINEA[i];
						LINEA[i] = '\0';
						rs = lookup_sym(&LINEA[s]);
						if (rs < 0) stovf = 1;
						LINEA[i] = (unsigned char)t;
						while (LINEA[i] == ' ') i++;
					} else {	/* monadic */
						rs = ms;
						ms = ls;
						ls = -1;
					}
				} else {
					ms = ls;	/* niladic */
					ls = -1;
				}
				s = i;
				loc = 0;
				while (s < ct) 	/* count locals */
				{
					if (LINEA[s] == ';') loc++;
					s++;
				}
				/* -1 already means "no result/left/right" here, so a
				   name that didn't fit is caught as it's looked up,
				   before anything is changed (V108). Before, the
				   function went into SYMT[-1]. */
				if (stovf)
				{
					fclose(ifp);
					free(tp);
					free(ltable);
					return(SET_ERR(ERR_STOVF));
				}
				if (on_stack(ms)) 	/* si damage */
				{
					fclose(ifp);
					free(tp);
					free(ltable);
					return(SET_ERR(ERR_SI));
				}
				sym_delete(ms);		/* rid any old fn or var */
				/* NOTE!!! stuff can move here, but better not after that */
				while (GarbCollect());		/* freeze stuff */
				SYMT[ms].type = SYM_FUN;
				fep = &SYMT[ms].value;
				l = (long)sizeof(struct Func0Ent) - (long)sizeof(struct MemEnt);
				loc--;
				if (loc > 0) l += loc * 2;
				f0e = (struct Func0Ent *)MakeEnt(l,ME_FUNC0,fep);
				if (!f0e) 
				{
					fclose(ifp);
					free(tp);
					free(ltable);
					return(SET_ERR(ERR_WSFULL));
				}
				f0e->f0e_result = zs;
				f0e->f0e_left = ls;
				f0e->f0e_right = rs;
				f0e->f0e_Line1 = 0;

				loc = 0;
				while (LINEA[i] == ';') 
				{
					i++;
					while (LINEA[i] == ' ') i++;
					s = i;
					while (is_varch(LINEA[i])) i++;
					t = LINEA[i];
					LINEA[i] = '\0';
					f0e->f0e_locals[loc] = lookup_sym(&LINEA[s]);
					if (f0e->f0e_locals[loc++] < 0) stovf = 1;
					LINEA[i] = (unsigned char)t;
					while (LINEA[i] == ' ') i++;
				}

				/* %%% seemed overkill, but Claude claimed it is not unprecidented */
				if (i < ct || stovf) 	/* leftover junk after the last local -- e.g. ';V,M' --
							   or a local that didn't fit (V108) */
				{
					sym_delete(ms);
					fclose(ifp);
					free(tp);
					free(ltable);
					return(SET_ERR(stovf ? ERR_STOVF : ERR_SYNTAX));
				}

				f0e->f0e_MemEnt.me_rank = loc;
				fep = &f0e->f0e_Line1;

				if (DEBUGFLAG) 
				{
					sprintf((char *)LINEA,"[0]  ");
					PRINT(LINEA);
					cp = LINEB;
					while (ct--) 
					{
						sprintf((char *)LINEA,"%02x ",*cp++);
						PRINT(LINEA);
					}
					NL();
				}

			} else {	/* not line 0 */
/* search the label table. A label is replaced by its line number only
   where a whole name matches it: the scan starts a match only where a
   name can start (is_name1()) and steps over the rest of a name
   (is_varch()). Until V107 both tests were letters only, so with a
   label LOOP, B2LOOP became B212 and MY_LOOP became MY_12 (the match
   started after the digit or the '_'); ∆ would have done the same. */
				qt = 0;
				cp = LINEB;
				t = 0;
				while (t < ct)
				{
					c = *cp;
					while (t < ct && (!is_name1(c) || qt))
					{
						if (c == CHAR_QT) qt = 1 - qt;
						cp++;
						t++;
						c = *cp;
					}
					lp = ltable;
					while (*lp) 
					{
						i = strlen(lp);
						if (!strncmp(cp,lp,i)) 
						{
							c = *(cp + i);
							if (!is_varch(c))
							{
/* insert line number */
								memcpy(LINEA,cp+i,((ct-t)+1)-i);
								ct -= i;		/* adjust count */
								lp += i + 1;
								i = *lp++ << 8;
								i += *lp;
								if (i > 99) 
								{
									*cp++ = '0' + i / 100;
									t++;
									ct++;
									i = i % 100;
								}
								if (i > 9) 
								{
									*cp++ = '0' + i / 10;
									t++;
									ct++;
								}
								*cp++ = '0' + i % 10;
								ct++;
								memcpy(cp,LINEA,(ct-t)+1);
								cp--;
								break;
							} 
						}
						lp += i + 3;
					}
					while (is_varch(*cp))
					{
						cp++;
						t++;
					}
				}
				if (DEBUGFLAG) 
				{
					sprintf((char *)LINEA,"[%d]  ",line);
					PRINT(LINEA);
					cp = LINEB;
					while (ct--) 
					{
						sprintf((char *)LINEA,"%02x ",*cp++);
						PRINT(LINEA);
					}
					NL();
				}
				/* for -q warn's notes (parse.c qsyn_note()):
				   which function and line is being compiled */
				qsyn_fn = ms;
				qsyn_line = line;
				fe = MakeFNEnt();  /* compiles LINEB into ws entry */
				qsyn_fn = -1;
				if (!fe) 
				{
					if (!ERROR_CODE) continue;
					else {
						UCHAR *gp;
						NL();
						sprintf((char *)LINEA,"[%d]  ",line);
						CPRINT(LINEA);
						/* translate raw file bytes -> display glyphs,
						   same as do_clist()'s ()LIST) handling */
						gp = (UCHAR *)tp;
						while (*gp) { *gp = orig_trans[*gp]; gp++; }
						FE_TRANS_FLAG++;
						vout((UCHAR *)tp,0,strlen(tp));
						FE_TRANS_FLAG--;
						CNL();
						/* delete the half-built function. Was
						   sym_delete(i): i is a leftover (the
						   header-parse offset, or a label's line
						   number), so this deleted an unrelated
						   symbol and left the function defined
						   (V77; tests/enter_syntax_error). */
						sym_delete(ms);
						fclose(ifp);
						free(tp);
						free(ltable);
						return(ERROR_CODE);
					}
				}
				fe->fe_MemEnt.me_reference = WS_OFF(fep);
				*fep = WS_OFF(fe);
				fep = &fe->fe_NextLine;
			}
			line++;
		}
		if (!fep) {		/* empty file: no header, so no function */
			fclose(ifp);
			i = SET_ERR(ERR_FNOTF);
			goto enterr;
		}
		*fep = 0;		/* terminate line list */
		fclose(ifp);
		while (*COMMANDP == ' ') COMMANDP++;
	}
	free(tp);
	free(ltable);
	return(0);
enterr:;
	free(tp);
	free(ltable);
	return(i);
}

/* ============================================================
   do_fsearch() -- )FSEARCH text (V77): every line of the function
   source files in $APLHOME/fns/ that contains text, as

       NAME[n]  line

   NAME is the file name without .fn, in upper case (as )FNSL shows
   it); n counts file lines as )ENTER and )LIST do, the header line
   being [0]; the line is displayed from the file as )ENTER's error
   display does. Files in name order; then a count.

   Matching is on font-index bytes -- the glyphs as typed and as
   displayed -- not on the translated (internal) form, because
   translation isn't one-to-one: itrans[] maps '$' and ⍞ both to '$',
   '-' and ¯ both to '-', and more. So:
   - each file line goes through orig_trans[] only (.fn byte to font
     index; ktrans[] is built from it and nothing installs another
     table), the same bytes )ENTER's error display shows;
   - the typed text is taken untranslated from last_input_line()
     (platform_translate.c: the last line alt_input_proc() was given),
     after the ')', the command word and spaces. To be sure that saved
     line is this command, its text is translated (text_translate())
     and must equal arg, the translated text do_command() passed; if
     not, Command Error rather than searching for the wrong thing.
   Comments, strings and partial quad names (⎕I) need no special
   handling. ASCII letters match either case. No wildcards. The search
   can be interrupted between lines.
   ============================================================ */
extern const UCHAR *last_input_line(void);	/* platform_translate.c */
extern int text_translate(UCHAR *cp, UCHAR *outs);

static int
fs_fold(c)
int c;
{
	if (c >= 'A' && c <= 'Z') c += 32;
	return(c);
}

/* 1 if pat (non-empty) occurs in hay, ASCII letters' case ignored */
static int
fs_contains(hay,pat)
const UCHAR *hay,*pat;
{
const UCHAR *h,*p;

	for (; *hay; hay++) {
		for (h = hay, p = pat; *p && *h && fs_fold(*h) == fs_fold(*p); h++, p++)
			;
		if (!*p) return(1);
	}
	return(0);
}

static int
fs_cmp(a,b)
const void *a,*b;
{
	return(strcmp((const char *)a, (const char *)b));
}

short
do_fsearch(arg)
UCHAR *arg;
{
struct stat st;
UCHAR pat[LINE_SIZE], chk[LINE_SIZE];
const UCHAR *rp;
UCHAR *cp, *raw, *work;
char dirbuf[PATHBUF_SIZE], sdir[PATHBUF_SIZE], path[PATHBUF_SIZE], cb[64];
char (*names)[64];
char *entry;
int plen, count, capacity, k, nl, fhit;
long hits, files;
short line, j;
FILE *ifp;

	/* the text as typed: the saved raw line past ")", the command
	   word (letters) and the spaces around them */
	rp = last_input_line();
	while (*rp == ' ') rp++;
	if (*rp) rp++;				/* the ')' */
	while (*rp == ' ') rp++;
	while ((*rp >= 'A' && *rp <= 'Z') || (*rp >= 'a' && *rp <= 'z')) rp++;
	while (*rp == ' ') rp++;
	plen = 0;
	while (rp[plen] && rp[plen] != 10 && rp[plen] != 13 && plen < LINE_SIZE - 1) {
		pat[plen] = rp[plen];
		plen++;
	}
	while (plen > 0 && pat[plen-1] == ' ') plen--;
	pat[plen] = '\0';
	if (!plen || !arg || !*arg) return(SET_ERR(ERR_COMMAND));
	/* is the saved line this command? */
	work = (UCHAR *)malloc(LINE_SIZE * 2);
	if (!work) return(SET_ERR(ERR_NOMEM));
	strcpy((char *)chk, (char *)pat);
	text_translate(chk, work);
	k = strcmp((char *)work, (char *)arg);
	free(work);
	if (k) return(SET_ERR(ERR_COMMAND));

	get_aplhome_dir(dirbuf, sizeof(dirbuf), "fns");	/* ends in a separator */
	/* stat() without the trailing separator: Windows's stat() fails
	   on "dir\\" (img_first_entry() strips it the same way) */
	strcpy(sdir, dirbuf);
	nl = (int)strlen(sdir);
	if (nl > 1 && img_is_sep(sdir[nl-1]) && !(nl == 3 && sdir[1] == ':'))
		sdir[nl-1] = '\0';
	if (stat(sdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		PRINT("Can't find: ");
		PRINT(dirbuf);
		NL();
		return(0);
	}

	/* the .fn files, sorted */
	count = 0;
	capacity = 64;
	names = (char (*)[64])malloc((size_t)capacity * sizeof(*names));
	if (!names) return(SET_ERR(ERR_NOMEM));
	for (entry = img_first_entry(dirbuf); entry; entry = get_next_dir()) {
		char *nm = entry + 1;

		if (entry[0] != 'F') continue;
		nl = (int)strlen(nm);
		if (nl < 4 || nl >= 64) continue;
		if (nm[nl-3] != '.' || fs_fold(nm[nl-2]) != 'f' || fs_fold(nm[nl-1]) != 'n')
			continue;
		if (count >= capacity) {
			char (*tmp)[64];
			capacity *= 2;
			tmp = (char (*)[64])realloc(names, (size_t)capacity * sizeof(*names));
			if (!tmp) break;
			names = tmp;
		}
		strcpy(names[count++], nm);
	}
	cleanup_dir();
	qsort(names, (size_t)count, sizeof(*names), fs_cmp);

	raw = (UCHAR *)malloc(LINE_SIZE);
	if (!raw) {
		free(names);
		return(SET_ERR(ERR_NOMEM));
	}

	hits = files = 0;
	for (k = 0; k < count && !ABORTSIG; k++) {
		if (strlen(dirbuf) + strlen(names[k]) >= sizeof(path)) continue;
		strcpy(path, dirbuf);
		strcat(path, names[k]);
		ifp = fopen(path, "r");
		if (!ifp) continue;
		line = 0;
		fhit = 0;
		while (!ABORTSIG && fgets((char *)raw, LINE_SIZE, ifp)) {
			{	/* strip line ending, as do_enter() does */
				size_t n = strlen((char *)raw);
				while (n > 0 && (raw[n-1] == 10 || raw[n-1] == 13)) raw[--n] = '\0';
			}
			/* .fn bytes to font index: what's displayed, and what
			   the same text typed would be */
			for (cp = raw; *cp; cp++) *cp = orig_trans[*cp];
			if (fs_contains(raw, pat)) {
				/* NAME[n]  then the line, as )ENTER's error display */
				nl = (int)strlen(names[k]) - 3;
				for (j = 0; j < nl && j < (short)sizeof(cb) - 1; j++)	/* ∆ for '=' (V107) */
					cb[j] = (char)fn_shown_char((UCHAR)filt_up((int)(UCHAR)names[k][j]));
				cb[j] = '\0';
				CPRINT(cb);
				sprintf(cb, "[%d]  ", line);
				CPRINT(cb);
				FE_TRANS_FLAG++;
				vout(raw, 0, strlen((char *)raw));
				FE_TRANS_FLAG--;
				CNL();
				hits++;
				fhit = 1;
			}
			line++;
		}
		fclose(ifp);
		if (fhit) files++;
	}
	free(raw);
	free(names);

	if (ABORTSIG) return(SET_ERR(ERR_INTERRUPT));
	if (!hits) CPRINT("Not found");
	else {
		sprintf(cb, "%ld line%s in %ld file%s", hits, hits == 1 ? "" : "s",
			files, files == 1 ? "" : "s");
		CPRINT(cb);
	}
	CNL();
	return(0);
}

short
do_width()
{
short i;

	if (*COMMANDP) 
	{
		COMMANDP = get_vstr(COMMANDP);
		if (COMMANDP) 
		{
			Cmessage(MSG_WAS);
			print_num((long)WIDTH);
			sscanf((char *)&VBUF[0],"%hd",&i);
			if (i < 20) i = 20;
			WIDTH = i;
			RMARGIN = LMARGIN + i;
		}
	} else {
		Cmessage(MSG_IS);
		print_num((long)WIDTH);
	}
	CNL();
	return(0);
}

short
do_digits()
{
short i;
short len;

	if (*COMMANDP) 
	{
		/* Hand-copy the token instead of using get_vstr() -- get_vstr()
		   is built for identifier-like tokens (workspace/function/symbol
		   names, "on"/"off") and rejects a token starting with '-',
		   which silently drops the whole command (no "Was" message, no
		   error, no change) for a negative DIGITS setting. Typed '-' is
		   identity through itrans[] (see filt_build_pattern() above), so
		   it arrives here as plain ASCII '-' and sscanf() handles it
		   directly -- same approach do_trace() already uses for its
		   numeric argument. */
		while (*COMMANDP == ' ') COMMANDP++;
		len = 0;
		while (*COMMANDP && *COMMANDP != ' ') VBUF[len++] = *COMMANDP++;
		VBUF[len] = '\0';

		if (len) 
		{
			Cmessage(MSG_WAS);
			print_num((long)DIGITS);
			sscanf((char *)&VBUF[0],"%hd",&i);
			/* DIGITS>0: fixed decimals, trailing zeros kept.
			   DIGITS<0: up to |DIGITS| decimals, trailing zeros trimmed
			   (see floatlen() in vprint.c). 0 is valid either way. */
			if (i > 17) i = 17;
			if (i < -17) i = -17;
			DIGITS = i;
		}
	} else {
		Cmessage(MSG_IS);
		print_num((long)DIGITS);
	}
	CNL();
	return(0);
}

/*
	)INTDIV [ON|OFF]

	Reports or sets whether dyadic divide truncates to an integer when
	both operands are integers (ON, this APL's original behavior) or
	always returns float (OFF, classic APL behavior, and the default
	for new/cleared workspaces -- see init_ws()). Same setting as
	⎕INTDIV (sysfuncs.c), where 1 = ON, 0 = OFF. Stored inverted as
	wspace->floatdiv (0 = ON), so workspaces saved before this setting
	existed load as ON -- see struct WorkSpace in parse.h.
	Saved/restored with the workspace header; nothing to sync in
	do_save()/load_ws().

	Argument parsing follows do_garb(): get_vstr() token, "on"/"off",
	anything else is ERR_SYNTAX. Upper case is accepted too, rather
	than relying on whatever case get_vstr() leaves in VBUF.
*/
short
do_intdiv()
{
short upd;

	if (*COMMANDP) 
	{
		COMMANDP = get_vstr(COMMANDP);
		if (!COMMANDP) return(SET_ERR(ERR_SYNTAX));
		upd = -1;
		if (!strcmp((char *)VBUF,"on")  || !strcmp((char *)VBUF,"ON"))  upd = 1;
		if (!strcmp((char *)VBUF,"off") || !strcmp((char *)VBUF,"OFF")) upd = 0;
		if (upd == -1) return(SET_ERR(ERR_SYNTAX));
		Cmessage(MSG_WAS);
		if (wspace->floatdiv) CPRINT("OFF");
		else CPRINT("ON");
		wspace->floatdiv = upd ? 0 : 1;
	} else {
		Cmessage(MSG_IS);
		if (wspace->floatdiv) CPRINT("OFF");
		else CPRINT("ON");
	}
	CNL();
	return(0);
}

/*
	)SEQUNIT [n] (V102, agreed with the person Oct 20, 2026)

	Reports or sets how many units a beat is divided into for the
	sequencer's timing values: ⎕SEQTRACK's steps and lengths (rows 2
	and 4) and result, ⎕SEQSTATE's positions and loop lengths,
	⎕SEQRUNTO, ⎕MIDIREAD's steps and lengths and ⎕MIDIINFO's track
	lengths. 1, the default, is beats. ⎕SEQTEMPO stays in beats a
	minute and ⎕SEQGATE a fraction of the step; ⎕SEQBEAT's beat
	numbers are beats. The conversions are all in seqfuncs.c
	(seqf_unit()); the library still works in beats and ticks.

	Saved with the workspace, as wspace->sequnit (0 meaning 1; see
	struct WorkSpace in parse.h), because a workspace's track
	matrices mean nothing without their unit. A command only, no
	quad function (agreed): it's set once for a workspace, not
	switched by programs.

	n must be one whole number, a divisor of 2520 (the sequencer's
	ticks per beat) from 1 to 255, so a whole number of units is
	always a whole number of ticks; anything else -- 16, a fraction,
	a negative, a name, a second number -- is a Domain Error and
	changes nothing (agreed: not rounded or clamped to a good value,
	which would quietly change all the timing; unlike )DIGITS'
	clamp and )INTDIV's Syntax Error). The check is seqf_unit_ok()'s,
	in seqfuncs.c with the library's tick count. Display follows
	do_digits(): "Is n" with no argument, "Was n" before setting.

	The token is read by hand rather than by get_vstr(), as
	do_digits() does: a typed ¯ arrives as '-' and get_vstr() would
	reject it without an error.
*/
extern int seqf_unit(void);		/* seqfuncs.c */
extern int seqf_unit_ok(long n);	/* seqfuncs.c */

short
do_sequnit()
{
UCHAR *cp;
long n;
int digits;

	cp = COMMANDP;
	while (*cp == ' ') cp++;
	if (!*cp)
	{
		Cmessage(MSG_IS);
		print_num((long)seqf_unit());
		CNL();
		return(0);
	}
	n = 0;
	digits = 0;
	while (*cp >= '0' && *cp <= '9')
	{
		if (n < 100000L) n = n * 10 + (*cp - '0');	/* no overflow;
							   too big anyway */
		cp++;
		digits++;
	}
	while (*cp == ' ') cp++;
	if (!digits || *cp || !seqf_unit_ok(n)) return(SET_ERR(ERR_DOMAIN));
	COMMANDP = cp;
	Cmessage(MSG_WAS);
	print_num((long)seqf_unit());
	CNL();
	wspace->sequnit = (UCHAR)(n == 1 ? 0 : n);
	return(0);
}

/*
	)STEP -- how ->0 continues a stopped function; a session setting
	(step_mode in exec.c, not saved with the workspace):
	  LINE  stop again at the start of the next line, stepping into
	        called functions
	  OP    stop after every compiled operation, reported as
	        "FN [n] what", e.g. "F [2] push var: X"
	  OFF   run on to the next )STOP line
	Only code a ->0 resumed steps (step_resumed in exec.c); a function
	freshly run from a typed line runs normally until a )STOP line.
	So stepping is ->0 repeatedly. Display/set follows do_intdiv(): no argument shows the
	mode, an argument shows the previous one and sets it, anything
	else is ERR_SYNTAX; either case accepted.
*/
static void
step_print(int m)
{
	if (m == STEP_LINE) CPRINT("LINE");
	else if (m == STEP_OP) CPRINT("OP");
	else CPRINT("OFF");
}

short
do_step()
{
short upd;

	if (*COMMANDP) 
	{
		COMMANDP = get_vstr(COMMANDP);
		if (!COMMANDP) return(SET_ERR(ERR_SYNTAX));
		upd = -1;
		if (!strcmp((char *)VBUF,"line") || !strcmp((char *)VBUF,"LINE")) upd = STEP_LINE;
		if (!strcmp((char *)VBUF,"op")   || !strcmp((char *)VBUF,"OP"))   upd = STEP_OP;
		if (!strcmp((char *)VBUF,"off")  || !strcmp((char *)VBUF,"OFF"))  upd = STEP_OFF;
		if (upd == -1) return(SET_ERR(ERR_SYNTAX));
		Cmessage(MSG_WAS);
		step_print(step_mode);
		step_mode = upd;
	} else {
		Cmessage(MSG_IS);
		step_print(step_mode);
	}
	CNL();
	return(0);
}

UCHAR
char_flip(c)
UCHAR c;
{
	if (c >= 'a' && c <= 'z') c -= 32;
	else if (c >= 'A' && c <= 'Z') c += 32;
	return(c);
}

/*
	print_wsid() -- the workspace name as names are shown (V108): the
	same flip print_name() gives )VARS and )FNS, so FOO shows as FOO in
	)WSID, )SAVE, )CLEAR and )RESIZE, as in )LIB. wsid holds the name
	as get_vstr() read it (lower case in the normal setting), which is
	also the .ws file name, so it isn't changed, only shown flipped.
	"Clear WS" is the placeholder, not a name: shown as it is.
*/
void
print_wsid(void)
{
char cb[WSID_SIZE];
short i;

	if (!symbolcase || !strcmp(wspace->wsid, "Clear WS"))
	{
		CPRINT(wspace->wsid);
		return;
	}
	for (i = 0; i < WSID_SIZE - 1 && wspace->wsid[i]; i++)
		cb[i] = (char)char_flip((UCHAR)wspace->wsid[i]);
	cb[i] = '\0';
	CPRINT(cb);
}

void
table_flip(cp)
UCHAR *cp;
{
	while (*cp) 
	{
		caseflip(cp);
		while (*cp) cp++;
		cp++;
	}
}

void
set_case(c)
short c;
{
UCHAR *cp,**np;
struct NameEnt *ne;
int i,j;

	if (c) {
		var_lo = 'A';
		var_hi = 'Z';
		alph_lo = 'a';
		alph_hi = 'z';
	} else {
		var_lo = 'a';
		var_hi = 'z';
		alph_lo = 'A';
		alph_hi = 'Z';
	}
	if (tablecase != c) {
		/* flip command table */
		table_flip(cmd_table);

		/* flip op table */
		cp = op_table;
		while (*cp) {
			*cp = char_flip(*cp);
			cp += 2;
		}

		/* flip func table */
		np = all_fnames;
		while (*np) table_flip(*np++);

		tablecase = c;
	}
	if (c != wspace->caseflip) 
	{
		/* flip symbol table */
		for (i=0;i<SYMTACTUAL;i++) 
		{
			j = SYMT[i].namelen;
			if (j) 
			{
				if (j <= SYMNAME_MAX) 
				{
					cp = (UCHAR *)&SYMT[i].name.c[0];			
				} else {
					ne = (struct NameEnt *)WS_PTR(SYMT[i].name.a);
					cp = ne->ne_data;
				}
				while (j--) { *cp = char_flip(*cp); cp++; }
			}
		}
		wspace->caseflip = c;
	}
}


/*
	do_purge() -- clear the SI completely: )PURGE, and init_ws() for
	)CLEAR. Pops frames until pop_fn() reports the stack empty.

	Used to stop after 1000 pops and report ERR_SI, which made a deep but
	intact SI (runaway recursion) impossible to clear -- and fatal under
	)CLEAR. No count is needed for the loop to end: pop_fn() removes at
	least the 3-word frame header each time (it may push back a 2-word
	result, which its next Adjust_Stack() clears), so MSTACK only moves
	toward STACKMAX. The check below is what's left of the old guard: a
	pop that doesn't move MSTACK up means a stack pop_fn() can't unwind,
	reported as before with ERR_SI. It can't happen with pop_fn() as it
	stands; it's there so a change to pop_fn() can't turn this into an
	endless loop.
*/
short
do_purge()
{
short t;
int32_t *before;

	for (t=0;t<NUM_FLOATVS;t++) FLOATVS[t] = 0;
	for (;;)
	{
		before = MSTACK;
		if (pop_fn()) return(0);		/* SI empty */
		if (MSTACK <= before) return(SET_ERR(ERR_SI));	/* no progress */
	}
}

void
init_ws()
{
REG short i;

	/* do_purge() fails only if the stack won't unwind (see there) -- a
	   damaged stack. The symbol table may then still hold a suspended
	   function's locals, and clearing on top of that isn't safe. (Was
	   message() + die(1); die() is an empty stub, so it carried on.) */
	if (do_purge())
		fatal_error("init_ws(): do_purge() could not clear the SI stack "
			"(a pop made no progress -- stack damaged)");
	wspace->water = wspace->begin;
	wspace->origin = 1;
	wspace->bootfn = -1;
	wspace->width = WIDTH;
	wspace->digits = DIGITS;
	wspace->floatdiv = 1;	/* )INTDIV OFF -- float divide, the default for
				   new/cleared workspaces (classic APL behavior).
				   Workspaces saved before )INTDIV existed carry 0
				   in this byte and still load as ON. */
	wspace->sequnit = 0;	/* )SEQUNIT 1: steps in beats (V102) */
	set_case(0);	/* see load_ws() */

	for (i=0;i<SYMTSIZE;i++) 
	{
		SYMT[i].type = SYM_UNDEF;
		SYMT[i].namelen = 0;
		SYMT[i].value = 0L;
		SYMT[i].name.a = 0L;
		SYMT[i].n2[0] = 0;		/* clear garbage */
		SYMT[i].n2[1] = 0;
	}
	strcpy(wspace->wsid,"Clear WS");
}

/*
	resize_ws_destructive() -- discard the current workspace and allocate
	a fresh block at the given size. Content is NOT preserved. Used by
	)CLEAR (which follows this with init_ws() to reset symbols/origin/
	wsid) and by load_ws() (which follows this by reading a saved blob
	directly over the fresh block, so init_ws() would be wasted work).

	Callers needing to keep the live pool across a resize must use
	mem_init()'s resize path instead -- see do_resize().
*/
static short
resize_ws_destructive(long newsize)
{
UCHAR *cp;
ws_off_t symt_base = (ws_off_t)sizeof(struct WorkSpace);
ws_off_t pool_base = symt_base +
                     (ws_off_t)(SYMTSIZE * sizeof(struct SymTable));

	wssize = MAX(4096L, newsize);
	cp = (UCHAR *)mem_alloc(wssize);
	if (!cp) return(-1);

	memset(cp, 0, (size_t)wssize);
	free((char *)wspace);

	wspace           = (struct WorkSpace *)cp;
	wspace->wssize   = (int32_t)wssize;
	wspace->begin    = pool_base;
	wspace->water    = pool_base;
	wspace->symtsize = SYMTSIZE;

	SYMT     = (struct SymTable *)((char *)wspace + symt_base);
	STACKMAX = (int32_t *)(((UCHAR *)wspace) + wssize);
	*--STACKMAX = STACK_EMARK;
	MSTACK   = STACKMAX;
	wsactual = wssize;

	SYMTACTUAL = SYMTSIZE;
	for (int i = 0; i < NUM_FLOATVS; i++) FLOATVS[i] = 0;

	return(0);
}

short
do_clearws(a)
long a;
{
    init_ws();
    if (a && a != wspace->wssize)
    {
        if (resize_ws_destructive(a)) return(-1);
        init_ws();      /* set origin, wsid, etc. on new workspace */
    }
    print_wsid();
    CNL();
    return(0);
}

short
do_resize(a)
long a;
{
	if (MSTACK != STACKMAX) return(SET_ERR(ERR_SI));
	if (!a) return(SET_ERR(ERR_COMMAND));		/* )RESIZE needs a size */

	if (a < (wspace->water + STACK_MARGIN))
		return(SET_ERR(ERR_WSFULL));		/* wouldn't leave usable headroom */

	if (a != wssize)
	{
		wssize = a;
		if (mem_init(NULL) != 1) return(SET_ERR(ERR_NOMEM));
	}

	print_wsid();
	CNL();
	return(0);
}

#ifndef APLBIG_ENDIAN
void
bswap_ws()
{
	long_swap((char *)&wspace->water);
	long_swap((char *)&wspace->wssize);
	long_swap((char *)&wspace->symtsize);
	short_swap((char *)&wspace->origin);
	short_swap((char *)&wspace->caseflip);
	short_swap((char *)&wspace->bootfn);
	short_swap((char *)&wspace->width);
	long_swap((char *)&wspace->begin);
}
#endif

/*
	adjust all ws pointers for saving
*/
void
mod_ws()
{
REG short i;
struct MemEnt *me,*md;
struct ValEnt *ve;
struct NameEnt *ne;
struct Func0Ent *f0e;
struct FuncEnt *fe;
int32_t l,m;
char *cp;
short *sp;
int32_t *lp;

    md = WS_PTR(wspace->water);
    me = WS_PTR(wspace->begin);

    /* pool entries: me_reference is already a ws_off_t -- nothing to adjust.
       On a big-endian machine we need to swap the fields for the file. */
#ifndef APLBIG_ENDIAN
    /* adjust timestamp endedness */
    for (i=0;i<6;i++) short_swap((char *)&wspace->tstamp[i]);

    while (me < md)
    {
        l = me->me_size;
        ve = (struct ValEnt *)me;
        ne = (struct NameEnt *)me;
        f0e = (struct Func0Ent *)me;
        fe = (struct FuncEnt *)me;
        i = me->me_rank;
        cp = DATA_AD(ve);
        long_swap((char *)&me->me_reference);
        long_swap((char *)&me->me_size);
        short_swap((char *)&me->me_rank);
        switch (me->me_flags & ME_TMASK)
        {
            case ME_VAL:
                m = ve->ve_length;
                long_swap((char *)&ve->ve_point);
                long_swap((char *)&ve->ve_length);
                while (i--) long_swap((char *)&ve->ve_rho[i]);
                if (!(me->me_flags & ME_MAL))
                {
                    switch (ve->ve_vtype & BYTES_MASK)
                    {
                        case 2:
                            m = m >> 1;
                            sp = (short *)cp;
                            while (m--) short_swap((char *)sp++);
                            break;
                        case 4:
                            m = m >> 2;
                            lp = (int32_t *)cp;
                            while (m--) long_swap((char *)lp++);
                            break;
                    }
                }
                break;
            case ME_NAM:
                short_swap((char *)&ne->ne_length);
                break;
            case ME_FUNC0:
                long_swap((char *)&f0e->f0e_Line1);
                short_swap((char *)&f0e->f0e_result);
                short_swap((char *)&f0e->f0e_left);
                short_swap((char *)&f0e->f0e_right);
                sp = f0e->f0e_locals;
                while (i--) short_swap((char *)sp++);
                break;
            case ME_FUNCN:
                long_swap((char *)&fe->fe_NextLine);
                break;
        }
        me = (struct MemEnt *)((char *)me + l);
    }
#endif

    /* adjust SymTable fields for endianness */
#ifndef APLBIG_ENDIAN
    for (i=0;i<SYMTACTUAL;i++)
    {
        long_swap((char *)&SYMT[i].value);
        if (SYMT[i].namelen > SYMNAME_MAX)
            long_swap((char *)&SYMT[i].name.a);
    }
    bswap_ws();
#endif
}

void
unmod_ws()
{
REG short i;
struct MemEnt *me,*md;
struct ValEnt *ve;
struct NameEnt *ne;
struct Func0Ent *f0e;
struct FuncEnt *fe;
int32_t l,m;
char *cp;
short *sp;
int32_t *lp;

#ifndef APLBIG_ENDIAN
    bswap_ws();
#endif

    md = WS_PTR(wspace->water);
    me = WS_PTR(wspace->begin);

#ifndef APLBIG_ENDIAN
    /* readjust timestamp endedness */
    for (i=0;i<6;i++) short_swap((char *)&wspace->tstamp[i]);

    while (me < md)
    {
        long_swap((char *)&me->me_reference);
        long_swap((char *)&me->me_size);
        short_swap((char *)&me->me_rank);
        l = me->me_size;
        ve = (struct ValEnt *)me;
        ne = (struct NameEnt *)me;
        f0e = (struct Func0Ent *)me;
        fe = (struct FuncEnt *)me;
        i = me->me_rank;
        switch (me->me_flags & ME_TMASK)
        {
            case ME_VAL:
                long_swap((char *)&ve->ve_point);
                long_swap((char *)&ve->ve_length);
                cp = DATA_AD(ve);
                m = ve->ve_length;
                while (i--) long_swap((char *)&ve->ve_rho[i]);
                if (!(me->me_flags & ME_MAL))
                {
                    switch (ve->ve_vtype & BYTES_MASK)
                    {
                        case 2:
                            m = m >> 1;
                            sp = (short *)cp;
                            while (m--) short_swap((char *)sp++);
                            break;
                        case 4:
                            m = m >> 2;
                            lp = (int32_t *)cp;
                            while (m--) long_swap((char *)lp++);
                            break;
                    }
                }
                break;
            case ME_NAM:
                short_swap((char *)&ne->ne_length);
                break;
            case ME_FUNC0:
                long_swap((char *)&f0e->f0e_Line1);
                short_swap((char *)&f0e->f0e_result);
                short_swap((char *)&f0e->f0e_left);
                short_swap((char *)&f0e->f0e_right);
                sp = f0e->f0e_locals;
                while (i--) short_swap((char *)sp++);
                break;
            case ME_FUNCN:
                long_swap((char *)&fe->fe_NextLine);
                break;
        }
        me = (struct MemEnt *)((char *)me + l);
    }

    /* unswap SymTable fields */
    for (i=0;i<SYMTACTUAL;i++)
    {
        long_swap((char *)&SYMT[i].value);
        if (SYMT[i].namelen > SYMNAME_MAX)
            long_swap((char *)&SYMT[i].name.a);
    }
#endif
}


char hashtab[] = {
	'C','o','p','y','r','i','g','h','t',' ','2','0','0','7',',',
	' ','K','e','i','t','h',' ','D','a','v','i','d',' ','D','o',
	'y','l','e',',',' ','A','l','l',' ','R','i','g','h','t','s',
	' ','R','e','s','e','r','v','e','d','.',' ',
	0
};



#ifdef FOR_POSTERITY
/* this was created because there were ws's that were somehow hashed with
   the inverse of the above table.   This can be determined by filezapping
   the ws and looking at the end to see if the copyright message is case
   flipped.

   Was able to fix by calling this on load, and then unswaping the hashtab on )save
   -- left here for posterity */
*/

int hf_flag = 0;

void
hashflip()
{
char c,*cp;

	if (hf_flag) return;
	cp = hashtab;
	while (*cp)
	{
		c = *cp;
		if (c >= 'A' && c <= 'Z') c += 32;
		else if (c >= 'a' && c <= 'z') c -= 32;
		*cp++ = c;
	}
	hf_flag = 1;
}
#endif

void
text_hash(s,l,d)
REG char *s;
REG long l;
int d;
{
REG int tabi;
char c,t;

#ifdef FOR_POSTERITY
hashflip();
#endif

	tabi = 0;
	while (l--) {
		c = hashtab[tabi++];
		if (!c) {
			tabi = 0;
			c = hashtab[tabi++];
		}
		if (d) {
			t = (*s ^ 0xff) + 1;
			*s++ = t ^ c;
		} else {
			t = *s ^ c;
			*s++ = (t ^ 0xff) + 1;
		}
	}
}

short
do_save()
{
int32_t l,n,o,v;
long r;
short i;
FILE *wfp;

    Adjust_Stack();
    wfp = NULL;
    while (GarbCollect());

    if (MSTACK != STACKMAX) return(SET_ERR(ERR_SI));

    /* a refused name (∆, V107) must not save under the old one; nor
       one that's another saved workspace's (V113): do_wsid() has said
       why, so no error on top. The rename comes before the Clear WS
       -> continue default (they were the other way round until V113),
       so a refused )SAVE NAME from a clear workspace leaves it named
       Clear WS rather than continue. A plain )SAVE as Clear WS still
       writes continue.ws, existing or not: classic CONTINUE. */
    if ((i = do_wsid(0)) != 0) {
        if (i == WSID_TAKEN) return(0);
        return(SET_ERR(ERR_COMMAND));
    }
	if (!strcmp("Clear WS",wspace->wsid)) strcpy(wspace->wsid,"continue");
    get_aplhome_dir((char *)LINEA, LINE_SIZE, "ws");
    strcat((char *)LINEA, wspace->wsid);
    strcat((char *)LINEA, ".ws");
    wfp = fopen((char *)LINEA,"wb");

    if (!wfp) return(SET_ERR(ERR_SAVE));

    stamp_ws();
    wspace->width = WIDTH;   /* sync current )WIDTH setting into header before save */
	wspace->digits = DIGITS;	/* ditto with )DIGITS setting */

    n = wspace->water;
    l = n - (int32_t)((char *)&wspace->water - (char *)wspace);
    wspace->water = l;      /* adjust to be rel to self */

    /* new magic number -- incompatible with pre-64bit format */
    o = 0x73777066L;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&o);
#endif
    if (4 != fwrite(&o,1,4,wfp))
    {
        fclose(wfp);
        return(SET_ERR(ERR_SAVE));
    }

    /* write workspace file format version */
    v = WS_FORMAT_VERSION;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&v);
#endif
    if (4 != fwrite(&v,1,4,wfp))
    {
        fclose(wfp);
        return(SET_ERR(ERR_SAVE));
    }

    /* write wspace size */
    o = wspace->wssize;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&o);
#endif

    mod_ws();       /* after this, can use saveerr */

    if (4 != fwrite(&o,1,4,wfp)) goto saveerr;

    /* write watermark (rel to &wspace->water) */
    o = l;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&o);
#endif
    if (4 != fwrite(&o,1,4,wfp)) goto saveerr;

    /* write the whole blob (header + SYMT + pool) hashed.
       SYMT is now part of the workspace block -- no separate write. */
    text_hash((char *)&wspace->water,l,1);
    r = (long)fwrite((char *)&wspace->water,1,l,wfp);
    text_hash((char *)&wspace->water,l,0);
    if (l != r) goto saveerr;

    fclose(wfp);
    unmod_ws();
    wspace->water = n;

    Cmessage(MSG_SAVED);
    print_stamp();
    print_wsid();
    CNL();
    return(0);
saveerr:;
    unmod_ws();
    wspace->water = n;
    if (wfp) fclose(wfp);
    return(SET_ERR(ERR_SAVE));
}

void
free_ws()
{
struct MemEnt *md,*me;
struct ValEnt *mal;

	md = WS_PTR(wspace->water);
	me = WS_PTR(wspace->begin);
	while (me < md) {
		if (me->me_flags & ME_MAL) {
				mal = (struct ValEnt *)me;
				if (mal->ve_point)
					sys_mfree((char *)(intptr_t)mal->ve_point,mal->ve_length);
				mal->ve_point = 0L;
		}
		me = (struct MemEnt *)((char *)me + me->me_size);
	}	
}

short
load_ws(s,f)
char *s;
int f;
{
int32_t ll,l,osize,v;
FILE *wfp;
short ret;

	ret = -1;
    get_aplhome_dir((char *)LINEA, LINE_SIZE, "ws");
    strcat((char *)LINEA, s);
    strcat((char *)LINEA, ".ws");
    strcpy((char *)VBUF, s);     /* keep display name in VBUF */
    strcat((char *)VBUF, ".ws");

	if (DEBUGFLAG)
	{
		NL();
		PRINT(LINEA);
		NL();
	}

    wfp = fopen((char *)LINEA,"rb");

    if (!wfp) { ret = 2; goto loaderr; }

    /* Read and validate the entire header before touching the live
       workspace in any way. A failed )LOAD must leave the current
       workspace exactly as it was -- free_ws() is deferred until every
       check below has passed. */

    /* read magic number */
    if (4 != fread(&l,1,4,wfp)) goto loaderr;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&l);
#endif
    ll = 0x73777066L;       /* new magic -- 64-bit clean format */
    if (l != ll)
    {
        ret = 1;            /* ws damaged or old format */
        goto loaderr;
    }

    /* read format version */
    if (4 != fread(&v,1,4,wfp)) goto loaderr;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&v);
#endif
    if (v != WS_FORMAT_VERSION) { ret = 3; goto loaderr; }   /* unsupported version */

    /* read wssize -- the size the workspace was saved/set at */
    if (4 != fread(&osize,1,4,wfp)) goto loaderr;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&osize);
#endif

    /* read watermark */
    if (4 != fread(&l,1,4,wfp)) goto loaderr;
#ifndef APLBIG_ENDIAN
    long_swap((char *)&l);
#endif

    /* Corruption guard only: does the file's own claimed content fit its
       own claimed size? )LOAD always restores the workspace to the size
       it was saved at -- )RESIZE is the tool for deliberately changing
       a workspace's size, so there's no longer any check against the
       active workspace's current size here. */
    if (l > (osize - STACK_MARGIN)) { ret = 4; goto loaderr; }

    free_ws();                     /* release old workspace's external refs
                                       before the block underneath it goes away */

    if (resize_ws_destructive(osize)) goto loaderr;   /* ret stays -1: out of memory */

    /* read the single blob -- header + SYMT + pool. Unlike every check
       above, a short/truncated read here happens after wspace has
       already started being overwritten -- it's the one failure mode
       that can leave the live workspace in a genuinely mixed,
       inconsistent state rather than simply untouched. */
    if (l != (long)fread((char *)&wspace->water,1,l,wfp)) { ret = 5; goto loaderr; }
    text_hash((char *)&wspace->water,l,0);

    fclose(wfp);
    unmod_ws();
    wspace->water += (int32_t)((char *)&wspace->water - (char *)wspace);

    /* The blob carries the saved workspace's own size, which should
       already match what we just resized to -- restore it explicitly
       anyway as a cheap guard against a corrupt/mismatched file. */
    wspace->wssize = (int32_t)wssize;

    /* SYMT pointer follows from wspace base -- no separate read needed */
    SYMT = (struct SymTable *)((char *)wspace + sizeof(struct WorkSpace));
	SYMTACTUAL = wspace->symtsize ? wspace->symtsize : SYMTSIZE;

    /* Always the normal case setting (caseflip 0). )CASE, removed in
       V81, could leave 1 here; set_case(0) then flips such a
       workspace's symbol names back as it loads, so it comes in
       normal. */
    set_case(0);
    WIDTH   = (wspace->width >= 20) ? wspace->width : 20;
    RMARGIN = LMARGIN + WIDTH;
	DIGITS = (wspace->digits >= 1 && wspace->digits <= 17) ? wspace->digits : 10;
    return(0);
loaderr:;
    if (wfp) fclose(wfp);
    return(ret);
}

/*
	show_loadmsg() -- ∆LOADMSG, a workspace's message for whoever loads
	it (V108; for demo workspaces). Called after )LOAD's "Saved" line,
	and by dll_startup() (main.c) for the startup workspace, before its
	boot function runs.

	An ordinary variable: set with ←, listed by )VARS, removed with
	)ERASE; nothing in the workspace header or file format. Shown only,
	never executed. A character vector is one line (wrapped at )WIDTH,
	which load_ws() has just restored), a character matrix one line per
	row, both printed by vprint() as if the name had been typed. Anything
	else -- a single character, an empty vector, a number, rank 3 or
	more, a function of that name, no such name -- shows nothing.

	find_sym(), not lookup_sym(): a workspace without the variable
	mustn't get an empty ∆LOADMSG entry added on every load (and saved).
	The name is built from var_lo, so it matches however set_case() has
	the letters stored (lower case in the normal setting).
*/
#define LOADMSG_NAME "LOADMSG"		/* after the ∆ */

void
show_loadmsg(void)
{
char nm[sizeof(LOADMSG_NAME) + 1];
const char *cp;
struct ValEnt *ve;
short i;

	if (SILENT_COMMAND) return;
	nm[0] = (char)CHAR_DELTA;
	for (cp = LOADMSG_NAME, i = 1; *cp; cp++, i++)
		nm[i] = (char)(var_lo + (*cp - 'A'));
	nm[i] = '\0';

	i = find_sym(nm);
	if (i < 0 || SYMT[i].type != SYM_VAR || !SYMT[i].value) return;
	ve = (struct ValEnt *)WS_PTR(SYMT[i].value);
	if ((ve->ve_vtype | VARIABLE_BIT) != QUOT_CODE) return;
	if (ve->ve_rank < 1 || ve->ve_rank > 2 || ve->ve_length < 1) return;

	if (CURSX) NL();
	vprint((struct MemEnt *)ve);
	NL();
}

short
do_load()
{
	strcpy((char *)VBUF,"continue");
	if (*COMMANDP) 
	{
		COMMANDP = get_vstr(COMMANDP);
		if (!COMMANDP)
		{
			goto loadrr;
		}
		/* no ∆ in workspace names yet (V107; see do_wsid()) */
		if (strchr((char *)VBUF, CHAR_DELTA))
			return(SET_ERR(ERR_COMMAND));
	}
	switch (load_ws(VBUF,0))
	{
		case 0:
			Cmessage(MSG_SAVED);
			print_stamp();
			CNL();
			show_loadmsg();
			return(0);
		case 1:
			/* damaged or unrecognized file -- current workspace untouched */
			return(SET_ERR(ERR_WSD)); 
		case 2:
			/* file not found -- current workspace untouched */
			return(SET_ERR(ERR_WSNOTF));	
		case 3:
			/* recognized file, unsupported version -- current workspace untouched */
			return(SET_ERR(ERR_WSD));	
		case 4:
			/* wouldn't fit in the active workspace -- current workspace untouched */
			return(SET_ERR(ERR_WSFULL));	
		case 5:
			/* truncated mid-read -- live workspace left inconsistent, reset to clean */
			free_ws();
			do_clearws(0);
			CNL();
			return(SET_ERR(ERR_WSD));	
	}

loadrr:;
	return(SET_ERR(ERR_WSLOAD));
}


short
do_erase()
{
short i;

	while (*COMMANDP) {
		COMMANDP = get_vstr(COMMANDP);
		if (!COMMANDP) return(SET_ERR(ERR_COMMAND));	/* error */
		i = find_sym((char *)VBUF);	/* not lookup_sym(): nothing to add (V108) */
		if (i >= 0) {
			if (on_stack(i)) return(SET_ERR(ERR_SI));
			sym_delete(i);
		}
		while (*COMMANDP == ' ') COMMANDP++;
	}
	while (GarbCollect());
	return(0);
}

/*
	ask_choice() (V113): asks the person to pick one of buttons, or
	cancel. buttons is one or more labels separated by '\n'; Cancel is
	always offered too, and is the default. Returns the chosen label's
	number, 1 up, or 0 for Cancel.

	The GUI shows a dialog: sys_confirm(), the same kind of link-time
	hook as sys_font_families() (declared in externs.h, defined in
	main.cpp; it takes utext, which is UTF-8). A front end with no
	dialog (apl_cli) returns -1, and the question is asked on the
	terminal instead, with ftext (font bytes, for terminal output):
	the text, then (Y/N) for one label or the labels' first letters
	and N for several, and one line read the way ⎕ input reads it.
	The answer's first letter that isn't a space picks; anything else,
	or end of input, is Cancel. Commands run only at the prompt, so
	nothing is running underneath either way.

	The answer line is recorded in the crash-log transcript, as typed
	input is, and CURSX goes back to 0 after it: the front end echoed
	the line and its newline itself (INVARIANTS.md, CURSX).
*/
static short
ask_choice(const char *title, const char *utext, const char *ftext,
	const char *buttons)
{
char keys[8];
char prompt[24];
UCHAR line[LINE_SIZE];
const char *bp,*tp;
short n,k,r;
UCHAR c;

	r = (short)sys_confirm(title, utext, buttons);
	if (r >= 0) return(r);

	/* no dialog: ask on the terminal */
	n = 0;
	bp = buttons;
	while (*bp && n < (short)sizeof keys) {
		c = (UCHAR)*bp;
		if (c >= 'a' && c <= 'z') c -= 32;
		keys[n++] = (char)c;
		while (*bp && *bp != '\n') bp++;
		if (*bp) bp++;
	}
	if (n == 1) keys[0] = 'Y';

	/* the text, a line at a time (CPRINT doesn't break lines) */
	tp = ftext;
	while (*tp) {
		k = 0;
		while (tp[k] && tp[k] != '\n') k++;
		if (k) {
			char lb[160];
			if (k > (short)sizeof lb - 1) k = (short)sizeof lb - 1;
			memcpy(lb, tp, k);
			lb[k] = '\0';
			CPRINT(lb);
		}
		CNL();
		tp += k;
		while (*tp && *tp != '\n') tp++;	/* rest of an over-long line */
		if (*tp) tp++;
	}

	k = 0;
	prompt[k++] = '(';
	for (r = 0; r < n; r++) {
		prompt[k++] = keys[r];
		prompt[k++] = '/';
	}
	prompt[k++] = 'N';
	prompt[k++] = ')';
	prompt[k++] = ' ';
	prompt[k] = '\0';
	CPRINT(prompt);

	line[0] = '\0';
	r = getin(line);
	CURSX = 0;
	if (!r) return(0);		/* end of input */
	crash_record_input(line);

	for (k = 0; line[k] == ' '; k++);
	c = line[k];
	if (c >= 'a' && c <= 'z') c -= 32;
	if (c == 'N') return(0);
	for (r = 0; r < n; r++)
		if (c == (UCHAR)keys[r]) return((short)(r + 1));
	return(0);
}

/* the file name part of a path, for the dialog text */
static const char *
path_base(const char *path)
{
const char *cp,*bp;

	bp = path;
	for (cp = path; *cp; cp++)
		if (*cp == '/' || *cp == '\\') bp = cp + 1;
	return(bp);
}

/*
	)DROP NAME (V113: workspaces too, with a question first).

	Deletes $APLHOME/ws/NAME.ws or $APLHOME/fns/NAME.fn -- before V113,
	only the function file. Whichever exists is named in a question
	(ask_choice(): a dialog in the GUI) that says what deleting it means;
	Cancel is the default. If both exist, the question asks which one.
	Neither: NOT FOUND and an Incorrect Command, as before.

	The two files are named as )SAVE/)LOAD and )ENTER name them: the
	workspace from the name as read (ws_file_path(), as do_save()), the
	function file lowered with ∆ as '=' (fn_file_path()). A name with ∆
	can't be a workspace's (do_wsid() refuses it), so only the function
	file is looked for then.

	Messages show the name as names are shown (shown_name()): FOO
	DROPPED. Until V113 the lowered name was printed as it was.
	A refusal -- Cancel -- is NOT DROPPED, with no error; a delete that
	fails is NOT DROPPED and an Incorrect Command.
*/
short
do_drop()
{
int i;
short ws,fn,which;
char wsname[WSID_SIZE];
char wspath[LINE_SIZE];
char fnpath[LINE_SIZE];
char uname[MAXSYM_CHARS * 3 + 8];	/* the name shown, UTF-8 (∆ is 3 bytes) */
char fname[MAXSYM_CHARS + 8];		/* the name shown, font bytes */
char utext[800];
char ftext[800];
const char *wsfile,*fnfile;

    if (!*COMMANDP) return(SET_ERR(ERR_SYNTAX));
    COMMANDP = get_vstr(COMMANDP);
    if (!COMMANDP) return(SET_ERR(ERR_COMMAND));

    /* the workspace file: the name as )SAVE would store it (V113) */
    ws = 0;
    if (!strchr((char *)VBUF, CHAR_DELTA)) {
        strncpy(wsname, (char *)VBUF, WSID_SIZE - 1);
        wsname[WSID_SIZE - 1] = '\0';
        ws_file_path(wspath, wsname);
        ws = (short)file_exists(wspath);
    }

    /* lowercase name to match file save convention */
    for (i = 0; VBUF[i]; i++)
        if (VBUF[i] >= 'A' && VBUF[i] <= 'Z') VBUF[i] += 32;

    fn_file_path(fnpath, (char *)VBUF);	/* ∆ as '=' (V107) */
    fn = (short)file_exists(fnpath);

    shown_name(fname, sizeof fname, (char *)VBUF, 0);
    shown_name(uname, sizeof uname, (char *)VBUF, 1);

    if (!ws && !fn) {
        CPRINT(fname);
        CPRINT(" NOT FOUND");
        CNL();
        return(SET_ERR(ERR_COMMAND));
    }

    wsfile = path_base(wspath);
    fnfile = path_base(fnpath);

    /* The question. Kept short (the person, V113). Each text twice:
       UTF-8 for the dialog, font bytes for apl_cli's terminal.
       Every %s has a precision, so the longest possible text fits
       utext/ftext (800) and gcc can see it: a name is at most 104
       bytes (uname), a file name is cut at 80 -- really at most
       MAXSYM_CHARS + 3, but it points into a LINE_SIZE path, and
       without the precision -O2's -Wformat-truncation warns. The
       longest, DROP_BOTH_TEXT, is 191 + 2*104 + 2*80 = 559 bytes. */
#define DROP_WS_TEXT \
	"Delete the saved workspace %.104s (%.80s)?\n\n" \
	"This deletes the whole workspace, not a function file."
#define DROP_FN_TEXT \
	"Delete the function file %.104s (%.80s)?\n\n" \
	"The function stays in the active workspace; use )ERASE %.104s " \
	"to remove it there."
#define DROP_BOTH_TEXT \
	"%.104s is both a saved workspace (%.80s) and a function file (%.80s). " \
	"Which one do you want to delete?\n\n" \
	"Deleting the function file leaves the function in the active " \
	"workspace; use )ERASE %.104s to remove it there."

    if (ws && fn) {
        snprintf(utext, sizeof utext, DROP_BOTH_TEXT, uname, wsfile, fnfile, uname);
        snprintf(ftext, sizeof ftext, DROP_BOTH_TEXT, fname, wsfile, fnfile, fname);
        i = ask_choice("Drop", utext, ftext, "Workspace\nFunction File");
        which = (i == 1) ? 1 : (i == 2) ? 2 : 0;
    } else if (ws) {
        snprintf(utext, sizeof utext, DROP_WS_TEXT, uname, wsfile);
        snprintf(ftext, sizeof ftext, DROP_WS_TEXT, fname, wsfile);
        which = ask_choice("Drop Workspace", utext, ftext, "Delete") ? 1 : 0;
    } else {
        snprintf(utext, sizeof utext, DROP_FN_TEXT, uname, fnfile, uname);
        snprintf(ftext, sizeof ftext, DROP_FN_TEXT, fname, fnfile, fname);
        which = ask_choice("Drop Function File", utext, ftext, "Delete") ? 2 : 0;
    }
#undef DROP_WS_TEXT
#undef DROP_FN_TEXT
#undef DROP_BOTH_TEXT

    if (!which) {
        CPRINT(fname);
        CPRINT(" NOT DROPPED");
        CNL();
        return(0);
    }
    if (unlink(which == 1 ? wspath : fnpath) == 0) {
        CPRINT(fname);
        CPRINT(" DROPPED");
        CNL();
        return(0);
    }
    CPRINT(fname);
    CPRINT(" NOT DROPPED");
    CNL();
    return(SET_ERR(ERR_COMMAND));
}

short
dump_ws(f)
short f;
{
struct MemEnt *me,*md;
char *bp,*cp;
long s;
ws_off_t t;
ws_off_t symt_lo,symt_hi;
short i,j;
char cb[4];

    cb[1] = '\0';
    if (f)
    {
        f = SILENT_COMMAND + 1;
        SILENT_COMMAND = 2;
    }
    symt_lo = sizeof(struct WorkSpace);
    symt_hi = symt_lo + (SYMTACTUAL * sizeof(struct SymTable));
    bp = (char *)LSCRATCH;
    sprintf(bp,"water: %04x, wsbeg: %04x, symtlo: %04x",
        wspace->water, wspace->begin, symt_lo);
    CPRINT(bp);
    CNL();
    md = WS_PTR(wspace->water);
    me = WS_PTR(wspace->begin);

    while (me < md)
    {
        CNL();
        switch (me->me_flags & ME_TMASK) {
            case ME_VAL:   CPRINT("VAL");        break;
            case ME_NAM:   CPRINT("NAME");       break;
            case ME_FUNC0: CPRINT("Func0");      break;
            case ME_FUNCN: CPRINT("Func");       break;
            case ME_ITMP:  CPRINT("Index Temp"); break;
            default:       CPRINT("???");        break;
        }
        if (me->me_flags & ME_FTRACE) CPRINT(" -ftrace");
        if (me->me_flags & ME_LTRACE) CPRINT(" -stop");
        if (me->me_flags & ME_MAL)    CPRINT(" -malloc");

        t = me->me_reference;
        CPRINT("      ref: ");
        cp = "Lost";
        if (!t)
        {
            cp = "Deleted";
        }
        else
        {
            /* verify back-reference is consistent */
            ws_off_t slot_val = *(ws_off_t *)((char *)wspace + t);
            if (slot_val == WS_OFF(me))
            {
                char *ref = (char *)wspace + t;
                if (ref >= (char *)MSTACK && ref <= (char *)STACKMAX)
                {
                    cp = "Stacked";
                }
                else if (t >= wspace->begin && t < wspace->water)
                {
                    sprintf(bp,"Mem ref - %04x", t);
                    cp = bp;
                }
                else if (t >= symt_lo && t < symt_hi)
                {
                    i = (int)((t - symt_lo) / sizeof(struct SymTable));
                    print_name(i);
                    cp = ", Symbol Table Ref";
                }
                else if (t == WS_OFF(&wspace->curfunc))
                {
                    cp = "Current Func line";
                }
            }
        }
        CPRINT(cp);

        s = me->me_size;
        cp = (char *)me;
        i = 16;
        while (s--)
        {
            if (i == 4 || i == 8 || i == 12) CPRINT(" ");
            if (i == 16)
            {
                CNL();
                sprintf(bp,"%04x  ",(unsigned int)WS_OFF(cp));
                CPRINT(bp);
                i = 0;
            }
            sprintf(bp,"%02x ",(unsigned char)*cp++);
            CPRINT(bp);
            if (i == 15 || !s)
            {
                cp -= i + 1;
                j = 15 - i;
                while (j--)
                {
                    CPRINT("   ");
                    if (j == 3 || j == 7 || j == 11) CPRINT(" ");
                }
                j = i + 1;
                while (j--)
                {
                    cb[0] = *cp++ & 0x7f;
                    if (cb[0] >= ' ' && cb[0] != 0x7f) CPRINT(cb);
                    else CPRINT(".");
                }
            }
            i++;
        }
        CNL();
        me = (struct MemEnt *)((char *)me + me->me_size);
    }
    CPRINT("-----------");
    CNL();
    if (f) SILENT_COMMAND = f - 1;
    return(0);
}

short
do_mem()
{
short i,s;
struct MemEnt *me,*md;
unsigned char *cp;
long l;
ws_off_t t;
char c,*tp;
ws_off_t symt_lo,symt_hi;

    tp = (char *)LSCRATCH;
    symt_lo = sizeof(struct WorkSpace);
    symt_hi = symt_lo + (SYMTACTUAL * sizeof(struct SymTable));
    l = (long)((char *)MSTACK - ((char *)wspace + wspace->water));
    l -= STACK_MARGIN;

    /* sprintf(tp,"%lx: %ld bytes available, water=%3x", (long)wspace,l,wspace->water); */
	sprintf(tp,"%p: %ld bytes available, water=%3x", (void*)wspace,l,wspace->water);

    PRINT(tp);
    NL();
    md = WS_PTR(wspace->water);
    me = WS_PTR(wspace->begin);

    while (me < md)
    {
        sprintf(tp,"%3x ",(unsigned int)WS_OFF(me));
        PRINT(tp);
        t = me->me_reference;
        c = '?';
        if (!t) c = '-';
        else {
            char *ref = (char *)wspace + t;
            if (ref >= (char *)MSTACK && ref <= (char *)STACKMAX) c = '#';
            else if (t >= wspace->begin && t < wspace->water) c = 'm';
            else if (t >= symt_lo && t < symt_hi) c = 's';
        }
        tp[0] = c;
        tp[1] = 0;
        PRINT(tp);
        s = -1;
        for (i=0;i<SYMTACTUAL;i++)
            if (SYMT[i].value && WS_PTR(SYMT[i].value) == me) s = i;
        if (s != -1)
        {
            if (me->me_reference != WS_OFF(&SYMT[s].value)) PRINT("#");
            else PRINT(" ");
            i = print_name(s);
            while (i<5)
            {
                PRINT(" ");
                i++;
            }
        } else PRINT("      ");
        PRINT(" ");
        switch (me->me_flags & ME_TMASK)
        {
            case ME_VAL:   PRINT("VAL"); break;
            case ME_NAM:   PRINT("NAM"); break;
            case ME_FUNC0: PRINT("F-0"); break;
            case ME_FUNCN: PRINT("F-N"); break;
            case ME_ITMP:  PRINT("ITM"); break;
            default:       PRINT("???"); break;
        }
        if (me->me_flags & ME_FTRACE) PRINT("t");
        if (me->me_flags & ME_LTRACE) PRINT("s");
        if (me->me_flags & ME_MAL) PRINT("+");
        else PRINT(" ");

		sprintf(tp,"%02x ",me->me_vtype);
		PRINT(tp);	


        i = sizeof(struct MemEnt);
        cp = (unsigned char *)me + i;
        l = me->me_size - i;
        if (l > 20L) l = 20L;
        while (l--)
        {
            sprintf(tp,"%02x ",*cp++);
            PRINT(tp);
        }
        NL();
        me = (struct MemEnt *)((char *)me + me->me_size);
    }
    return(0);
}


/*
	do_si() -- )SI (f 0) and )SIV (f 1). Unchanged; the walk is
	si_walk()'s, with no cap.
*/
short si_walk(short f, long newest, long oldest);

short
do_si(f)
short f;
{
	return(si_walk(f, -1L, -1L));
}

/*
	si_walk() -- print the SI, newest frame first (MSTACK up to
	STACKMAX), as )SI/)SIV always have. With newest >= 0 the listing is
	capped for the crash log (crashlog.c): the first `newest` frames and
	the last `oldest`, with "... n frames omitted ..." between, when there
	are more than that -- a runaway recursion may be exactly what went
	wrong, and thousands of identical lines would bury the rest. The cap
	needs the total first, so a capped walk makes a counting pass. A
	frame is a function frame -- one that prints -- when its fnline is
	nonzero; only those are counted.

	)SI/)SIV start with Adjust_Stack(), as they always have: it zeroes
	whatever sits above the top frame mark, harmless at the prompt. A
	capped walk must not: the crash log can be written while the session
	carries on (a warning entry, from inside GarbCollect()), and then
	those words are the operands of the operation under way. So a capped
	walk writes nothing -- it steps over them, the same way the frame
	scan below steps over temporaries, to reach the first frame mark.

	Both passes also stop at STACKMAX in the inner scan for the next
	frame mark. On an intact stack that changes nothing (the scan finds
	a mark no later than STACKMAX, which Adjust_Stack() relies on too),
	but the crash log walks a stack that may be damaged, and a missing
	mark would otherwise run the scan off the end. The "no room for a
	frame" check is made only on a capped walk, so )SI/)SIV are
	untouched even there.
*/
short
si_walk(short f, long newest, long oldest)
{
int32_t *sp,l,t;
short fline,fnum,locals,j;
struct Func0Ent *f0e;
long total,k,omitted;
char cb[48];
int32_t *start;

	start = MSTACK;
	if (newest < 0)
	{
		Adjust_Stack();		/* )SI / )SIV, as always */
		start = MSTACK;
	}
	else			/* capped: read-only, see above */
		while (start < STACKMAX && *start != STACK_EMARK)
		{
			if (*start == STACK_TEMP_VAL) start++;
			start++;
		}

	omitted = 0;
	if (newest >= 0)
	{
		if (oldest < 0) oldest = 0;
		total = 0;
		sp = start;
		while (sp < STACKMAX && sp + 2 < STACKMAX)
		{
			sp++;			/* emark */
			l = *sp++;		/* fnline */
			sp++;			/* CURFUNC or fnhead */
			if ((short)((l >> FLINE_SHIFT) & FLINE_MASK))
			{
				sp += (short)((l >> LVAR_SHIFT) & LVAR_MASK);
				total++;
			}
			while (sp < STACKMAX && *sp != STACK_EMARK)
			{
				if (*sp == STACK_TEMP_VAL) sp++;
				sp++;
			}
		}
		if (total > newest + oldest) omitted = total - newest - oldest;
	}

	k = 0;
	sp = start;
	while (sp < STACKMAX)
	{
		if (newest >= 0 && sp + 2 >= STACKMAX) break;	/* damaged: no room for a frame */
		sp++;	/* skip over emark */
		l = *sp++;	/* get fnline */
		t = *sp++;	/* get CURFUNC or fnhead */
		fline = (short)((l >> FLINE_SHIFT) & FLINE_MASK);
		fnum = -1;
		locals = 0;
		if (fline)
		{
			fnum = (short)((t >> FNUM_SHIFT) & FNUM_MASK);
			f0e = (struct Func0Ent *)WS_PTR(SYMT[fnum].value);
			locals = f0e->f0e_MemEnt.me_rank;
			j = (short)((l >> LVAR_SHIFT) & LVAR_MASK);	/* act locs */
			sp += j;
			if (omitted && k == newest)
			{
				snprintf(cb, sizeof(cb), "... %ld frames omitted ...", omitted);
				PRINT(cb);
				NL();
			}
			if (!omitted || k < newest || k >= newest + omitted)
			{
				print_name(fnum);
				PRINT(" [");
				print_num((long)fline);
				PRINT("] ");
				if (f)
				{
					while (locals--) {
						print_name(f0e->f0e_locals[locals]);
						PRINT(" ");
					}
					if (f0e->f0e_left != -1) {
						print_name(f0e->f0e_left);
						PRINT(" ");
					}
					if (f0e->f0e_right != -1) {
						print_name(f0e->f0e_right);
						PRINT(" ");
					}
					if (f0e->f0e_result != -1) {
						print_name(f0e->f0e_result);
						PRINT(" ");
					}
				} else PRINT("* ");
				NL();
			}
			k++;
		}
		while (sp < STACKMAX && *sp != STACK_EMARK)
		{
			if (*sp == STACK_TEMP_VAL) sp++;
			sp++;
		}
		if (ABORTSIG) return(SET_ERR(ERR_INTERRUPT));
	}
	l = 0;
	for (j=0;j<NUM_FLOATVS;j++) if (FLOATVS[j]) l++;
	if (l) {
		print_num((long)l);
		PRINT(" active FLOATVS");
		NL();
	}
	return(0);
}

short
do_stack()
{
int32_t *sp;
int32_t l;
char *cp;
	
	cp = (char *)LSCRATCH;
	sp = MSTACK;
	printf("Stack: %p",(void *)sp);
	NL();
	while (sp < STACKMAX) {
		l = *sp++;
		sprintf(cp,"%08x ",(unsigned int)l);
		PRINT(cp);
		if (l & STACK_TEMP_VAL) {
			l = *sp++;
			sprintf(cp,"%08x ",(unsigned int)l);
			PRINT(cp);
		}
		NL();
	}
	return(0);
}

/*
	clist_linenum() -- if the last word of s is a plain line number,
	remove it from s and return it; otherwise leave s alone and return
	-1. Leading zeros are ignored; more than 5 significant digits
	returns 99999, which no function has, so it's reported as out of
	range rather than as a bad name. At least one word must remain.
*/
static long
clist_linenum(char *s)
{
char *word, *dp, *end;
int j;

	word = strrchr(s, ' ');
	if (!word) return(-1L);		/* only one word: that's the name */
	end = word;			/* where the name will end */
	word++;
	if (!*word) return(-1L);
	for (j = 0; word[j]; j++)
		if (word[j] < '0' || word[j] > '9') return(-1L);
	while (end > s && end[-1] == ' ') end--;
	if (end == s) return(-1L);	/* nothing left for a name */
	dp = word;
	while (*dp == '0' && dp[1]) dp++;
	j = (int)strlen(dp);
	*end = '\0';			/* cut the number (and blanks) off s */
	if (j > 5) return(99999L);
	return(atol(dp));
}

/*
	do_clist() -- )LIST. (Until V81 it also listed help.dat for )HELP,
	which is where its old f argument and its "a name with a / is a path"
	rule came from; both went with )HELP.)

	)LIST fname          whole file, header first as line 0
	)LIST fname n        just line n (0 = the header)
	)LIST fname n m      lines n through m

	Line numbers are taken from the end of the argument: if the last
	one or two words are plain numbers they're line numbers and the
	rest is the name. A name ending in digits (FUNC1) is one word, so
	it's unaffected; only a .fn file whose base name had a blank and
	then a number could be misread, and no function can have such a
	name. A range that runs backwards is a Syntax Error;
	a line past the end prints "No line 9 (lines are 0 to 5)" and is a
	Value Error, as for )TRACE. The numbers are the file's, which match
	the function in the workspace as long as the file hasn't changed
	since it was )ENTERed -- )ENTER counts every file line too.
*/
short
do_clist(char *s)
{
FILE *lfp;
short i,j;
char *cp;
long first, last, n2;
int ranged, nlines;
	
	cp = (char *)LSCRATCH;
	/* strip trailing blanks; checks length first, so an empty argument
	   doesn't read s[-1] */
	i = strlen(s);
	while (i > 0 && s[i-1] == ' ') s[--i] = '\0';
	if (!*s) return(SET_ERR(ERR_COMMAND));

	ranged = 0;
	first = 0;
	last = 0;
	{
		last = clist_linenum(s);
		if (last >= 0)
		{
			ranged = 1;
			first = last;
			n2 = clist_linenum(s);
			if (n2 >= 0) first = n2;
			if (first > last) return(SET_ERR(ERR_SYNTAX));
		}
	}

	fn_file_path(cp, s);	/* lowered, ∆ as '=' (V107) */
	lfp = fopen(cp,"r");
	if (!lfp) return(SET_ERR(ERR_FNOTF));

	if (ranged)		/* check the range against the file first */
	{
		nlines = -1;	/* the header is line 0 */
		while (fgets((char *)LINEA,LINE_SIZE-1,lfp)) nlines++;
		if (last > nlines)
		{
			fclose(lfp);
			if (nlines < 0) nlines = 0;
			sprintf(cp, "No line %ld (lines are 0 to %d)", (first > nlines) ? first : last, nlines);
			PRINT(cp);
			NL();
			return(SET_ERR(ERR_VALUE));
		}
		rewind(lfp);
	}

	i = 0;
	while (fgets((char *)LINEA,LINE_SIZE-1,lfp)) 
	{
		if (ranged && i > last) break;
		if (ranged && i < first)
		{
			i++;
			continue;
		}
		j = strlen(LINEA);
		while (j > 0 && (LINEA[j-1] == 10 || LINEA[j-1] == 13)) j--;
		LINEA[j] = '\0';
		{
			UCHAR *tp = LINEA;
			while (*tp) { *tp = orig_trans[*tp]; tp++; }
		}
		{
			if (i)  // don't number line 0?  Better if it's there
			{
				sprintf(cp,"[%d] ",i);
				PRINT(cp);
			} else PRINT("     \xc7");	// prefix with del
			if (i < 10) PRINT(" ");
			CURSX += 5;
		}

		FE_TRANS_FLAG++;
		vout(LINEA,0,strlen(LINEA));
		FE_TRANS_FLAG--;

		NL();
		i++;
	}

	fclose(lfp);
	return(0);
}

short
do_garb()
{
int upd;

	if (*COMMANDP) 
	{
		COMMANDP = get_vstr(COMMANDP);
		if (COMMANDP) 
		{
			/* check for valid param */
			upd = -1;
			if (!strcmp(VBUF,"on")) upd = 1;
			if (!strcmp(VBUF,"off")) upd = 0;
			if (upd == -1) return(SET_ERR(ERR_SYNTAX));
			else {
				/* if so, report was & set */
				Cmessage(MSG_WAS);
				if (garb_debug) CPRINT("ON");
				else CPRINT("OFF");
				NL();
				garb_debug = upd;
			}
			return(0);
		}
	}
	/* no param, do it the old way */
	upd = GarbCollect();
	CPRINT("Gained ");
	print_num(upd);
	CNL();
	return(0);
}

/* ============================================================
   )TRACE and )STOP
   ============================================================
   Both commands work on one flag bit in each function line's
   MemEnt: ME_FTRACE for )TRACE (cnum 31), ME_LTRACE for )STOP
   (cnum 30, which was )LTRACE; the bit keeps its old name). )TRACE
   shows each flagged line's result; )STOP suspends the function
   before a flagged line runs (see lexec() in exec.c). Every form
   ends by showing the resulting state, prefixed "Now: " when the
   command changed something, so a change and a query look different:

     )TRACE              list every function with traced lines
     )TRACE OFF          clear every trace in the workspace
     )TRACE F            show F's traced lines, e.g. F[1 3]
     )TRACE F OFF        clear all of F's traces
     )TRACE F 1 3 ...    toggle each listed line

   OFF is always the keyword, so a function named OFF can't be
   traced. Line numbers must be 1 to the function's last line
   (line 0 is the header; nothing reads a flag there). Every
   token is checked before any line is changed, so a bad command
   changes nothing. A malformed argument is a Syntax Error; a name
   that isn't a function, or a line the function doesn't have, is a
   Value Error, preceded by a line saying what was wrong. The flags live on the lines themselves, so
   listing them means walking the functions -- no separate list
   to keep in step with )ERASE, )ENTER, )LOAD etc.
   ============================================================ */

/* the flag bit a trace command works on */
static UCHAR trace_bit(short n)
{
	return (n == 30) ? ME_LTRACE : ME_FTRACE;
}

/* true if VBUF holds the OFF keyword; both cases accepted, as
   do_intdiv() does for ON/OFF */
static int trace_is_off(void)
{
	return !strcmp((char *)VBUF,"OFF") || !strcmp((char *)VBUF,"off");
}

/* copy the next blank-delimited token at *pp into buf and advance
   *pp past it; returns its length (0 = no more tokens), or -1 if it
   doesn't fit in buf */
static int trace_token(UCHAR **pp, char *buf, int size)
{
UCHAR *cp;
int len;

	cp = *pp;
	while (*cp == ' ') cp++;
	len = 0;
	while (*cp && *cp != ' ')
	{
		if (len >= size - 1) return(-1);
		buf[len++] = (char)*cp++;
	}
	buf[len] = '\0';
	*pp = cp;
	return(len);
}

/* number of lines in function fnum, not counting the header */
static int trace_line_count(short fnum)
{
struct FuncEnt *fe;
int count;

	fe = (struct FuncEnt *)WS_PTR(SYMT[fnum].value);
	count = 0;
	while (fe->fe_NextLine)
	{
		fe = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
		count++;
	}
	return(count);
}

/* line `line` (1 = first line after the header) of function fnum;
   caller has already checked 1 <= line <= trace_line_count() */
static struct FuncEnt *trace_line(short fnum, int line)
{
struct FuncEnt *fe;

	fe = (struct FuncEnt *)WS_PTR(SYMT[fnum].value);
	while (line--) fe = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
	return(fe);
}

/* how many of function fnum's lines carry flag (header not counted) */
static int trace_flag_count(short fnum, UCHAR flag)
{
struct FuncEnt *fe;
int count;

	fe = (struct FuncEnt *)WS_PTR(SYMT[fnum].value);
	count = 0;
	while (fe->fe_NextLine)
	{
		fe = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
		if (fe->fe_MemEnt.me_flags & flag) count++;
	}
	return(count);
}

/* display function fnum's traced lines as NAME[1 3], or NAME[] */
static void trace_show_fn(short fnum, UCHAR flag)
{
struct FuncEnt *fe;
int line, first;

	print_name(fnum);
	CPRINT("[");
	fe = (struct FuncEnt *)WS_PTR(SYMT[fnum].value);
	line = 0;
	first = 1;
	while (fe->fe_NextLine)
	{
		fe = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
		line++;
		if (fe->fe_MemEnt.me_flags & flag)
		{
			if (!first) CPRINT(" ");
			print_num((long)line);
			first = 0;
		}
	}
	CPRINT("]");
	CNL();
}

/* clear flag on every entry of function fnum, the header included --
   an old )TRACE F 0 could have set it there, where nothing shows it */
static void trace_clear_fn(short fnum, UCHAR flag)
{
struct FuncEnt *fe;

	fe = (struct FuncEnt *)WS_PTR(SYMT[fnum].value);
	fe->fe_MemEnt.me_flags &= ~flag;
	while (fe->fe_NextLine)
	{
		fe = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
		fe->fe_MemEnt.me_flags &= ~flag;
	}
}

/* true if symbol i is a function a trace command can work on */
static int trace_is_fn(short i)
{
struct MemEnt *me;

	if (i < 0 || !SYMT[i].namelen) return(0);
	if (SYMT[i].type != SYM_FUN || !SYMT[i].value) return(0);
	me = WS_PTR(SYMT[i].value);
	return((me->me_flags & ME_TMASK) == ME_FUNC0);
}

/* )TRACE / )STOP with no argument: every function with at least
   one flagged line, one per line, in name order (sorted the same way
   do_list() sorts )FNS); "None" if there aren't any */
static short trace_list_all(UCHAR flag)
{
short *indices;
int nind, k;
short i;

	indices = (short *)malloc((size_t)SYMTACTUAL * sizeof(short));
	if (!indices) return(SET_ERR(ERR_NOMEM));

	nind = 0;
	for (i = 0; i < SYMTACTUAL; i++)
		if (trace_is_fn(i) && trace_flag_count(i, flag)) indices[nind++] = i;

	qsort(indices, (size_t)nind, sizeof(short), sym_name_cmp);

	if (!nind)
	{
		CPRINT("None");
		CNL();
	}
	for (k = 0; k < nind; k++) trace_show_fn(indices[k], flag);

	free(indices);
	return(0);
}

short
do_trace(n)
short n;
{
UCHAR flag;
UCHAR *args, *cp;
short f;
int nlines, len, pass, j, toggled;
long line;
char tok[64];

	flag = trace_bit(n);

	while (*COMMANDP == ' ') COMMANDP++;
	if (!*COMMANDP) return(trace_list_all(flag));	/* )TRACE */

	COMMANDP = get_vstr(COMMANDP);
	if (!COMMANDP) return(SET_ERR(ERR_SYNTAX));

	if (trace_is_off())		/* )TRACE OFF -- nothing may follow */
	{
		cp = COMMANDP;
		if (trace_token(&cp, tok, sizeof(tok))) return(SET_ERR(ERR_SYNTAX));
		for (f = 0; f < SYMTACTUAL; f++)
			if (trace_is_fn(f)) trace_clear_fn(f, flag);
		CPRINT("Now: None");	/* all that trace_list_all() could show */
		CNL();
		return(0);
	}

	f = lookup_sym(VBUF);
	if (!trace_is_fn(f))
	{
		if (f >= 0)
		{
			print_name(f);
			CPRINT(" is not a function");
			CNL();
		}
		return(SET_ERR(ERR_VALUE));
	}
	nlines = trace_line_count(f);
	args = COMMANDP;

	/* )TRACE F OFF -- OFF must be the only thing after the name */
	cp = args;
	len = trace_token(&cp, tok, sizeof(tok));
	if (len > 0 && (!strcmp(tok,"OFF") || !strcmp(tok,"off")))
	{
		if (trace_token(&cp, tok, sizeof(tok))) return(SET_ERR(ERR_SYNTAX));
		trace_clear_fn(f, flag);
		CPRINT("Now: ");
		trace_show_fn(f, flag);
		return(0);
	}

	/* )TRACE F [n ...] -- pass 0 checks every token, pass 1 toggles,
	   so a bad token anywhere leaves every line unchanged. A token
	   that isn't a plain number is a Syntax Error; a number that
	   isn't one of F's lines is a Value Error, after a line saying
	   how many lines F has. Leading zeros are ignored; more than 5
	   significant digits can't be a line (FLINE_MASK allows 4095)
	   and isn't passed to atol(). */
	toggled = 0;
	for (pass = 0; pass < 2; pass++)
	{
		cp = args;
		while ((len = trace_token(&cp, tok, sizeof(tok))) != 0)
		{
			if (len < 0) return(SET_ERR(ERR_SYNTAX));
			for (j = 0; j < len; j++)
				if (tok[j] < '0' || tok[j] > '9') return(SET_ERR(ERR_SYNTAX));
			j = 0;
			while (j < len - 1 && tok[j] == '0') j++;
			line = (len - j > 5) ? 0L : atol(&tok[j]);
			if (line < 1 || line > nlines)
			{
				print_name(f);
				CPRINT(" has no line ");
				CPRINT(tok);
				CPRINT(" (it has ");
				print_num((long)nlines);
				CPRINT((nlines == 1) ? " line)" : " lines)");
				CNL();
				return(SET_ERR(ERR_VALUE));
			}
			if (pass)
			{
				trace_line(f, (int)line)->fe_MemEnt.me_flags ^= flag;
				toggled = 1;
			}
		}
	}
	COMMANDP = cp;

	if (toggled) CPRINT("Now: ");
	trace_show_fn(f, flag);
	return(0);
}

short
print_stack(n)
short n;
{
int32_t *lp;

	Adjust_Stack();
	lp = MSTACK;

	if (MSTACK < STACKMAX) MSTACK += 2;
	if ((*MSTACK & 0xf0000000) != STACK_FNHEAD) {
		MSTACK = lp;
		return(0);
	}
	MSTACK++;
	VTEMP = get_vparm(n);
	MSTACK = lp;
	if (!VTEMP) return(0);
	if (!*VTEMP) PRINT("Undefined");
	else vprint(WS_PTR(*VTEMP));
	return(1);
}

void
set_commandp(unsigned char *cp)
{
	COMMANDP = cp;
}

short
do_command(line)
UCHAR *line;
{
UCHAR *cp,*cp2;
UCHAR bf[20];
short cnum,j;
int i;
long l;

	cp2 = bf;
	cp = line+1;
	while (*cp == ' ') cp++;
	while (*cp >= var_lo && *cp <= var_hi) *cp2++ = *cp++;
	while (*cp == ' ') cp++;
	COMMANDP = cp;	/* remember where command ends */
	*cp2 = 0;

	cp = cmd_table;

	cnum = -1;
	i = 0;
	while (*cp != '@') {
		cp2 = bf;
		while (*cp) {
			if (*cp == *cp2) {
				cp++;
				cp2++;
			} else break;
		}
		if (!*cp) { cnum = i; break; }
		i++;
		while (*cp++);
	}
	switch (cnum) 
	{
		case 0:		/* OFF */
			return(SET_ERR(ERR_KILL));
		case 1:		/* VARS */
			return(do_vars(COMMANDP));
		case 2:		/* DEBUG */
			Cmessage(MSG_WAS);
			if (DEBUGFLAG) Cmessage(MSG_ON);
			else Cmessage(MSG_OFF);
			DEBUGFLAG = 1-DEBUGFLAG;
			CNL();
			return(0);
		case 3:		/* DUMP */
			i = dump_ws(1);
			if (dbgf) fclose(dbgf);
			dbgf = 0L;
			return(i);
		case 4:		/* CLEAR */
			l = 0L;
			if (*COMMANDP) sscanf((char *)COMMANDP,"%ld",&l);
			return(do_clearws(l));
		case 5:		/* WSID */
			i = do_wsid(1);
			/* a name taken by another saved workspace: refused
			   and reported (V113), not an error */
			return((i == WSID_TAKEN) ? 0 : i);
		case 6:		/* GARB */
			return(do_garb());
		case 7:		/* SAVE */
			return(do_save());
		case 8:		/* LOAD */
			return(do_load());
		case 9:		/* ERASE */
			return(do_erase());
		case 10:	/* FNSL */
			get_aplhome_dir((char *)LINEA, LINE_SIZE, "fns");
			if (*COMMANDP) {
				/* Session 2 fix: this argument used to be silently
				   ignored -- )FNSL <anything> always listed every
				   .fn file. Now routed through the same
				   filt_build_pattern() grammar )VARS/)FNS/)SYMS use
				   (auto-trailing-'*', CHAR_POW->'*' translation),
				   then FN_POSTFIX is appended unconditionally so the
				   .fn extension requirement still holds -- any
				   redundant '*' this produces (e.g. "A**.fn") is
				   harmless, filt_match() collapses runs of '*'. */
				char fbuf[FILT_PAT_SIZE];
				filt_build_pattern((const char *)COMMANDP, fbuf, sizeof(fbuf));
				strcat(LINEA, fbuf);
			}
			strcat(LINEA,FN_POSTFIX);
			calldir_fns = 1;	/* show ∆, not '=' (V107) */
			calldir((char *)LINEA);
			calldir_fns = 0;
			return(0);
		case 11:	/* FNS */
			return(do_fns(COMMANDP));
		case 12:	/* SYMS */
			return(do_syms(COMMANDP));
		case 13:	/* MEM */
			return(do_mem());
		case 14:	/* ORIGIN */
			if (*COMMANDP) 
			{
				sscanf((char *)COMMANDP,"%d",&i);
				Cmessage(MSG_WAS);
				print_num((long)wspace->origin);
				wspace->origin = i;
			} else {
				Cmessage(MSG_IS);
				print_num((long)wspace->origin);
			}
			CNL();
			return(0);
		case 15:	/* WIDTH */
			return(do_width());
		case 16:	/* ENTER */
			return(do_enter());
		case 17:	/* EDIT */
            if (!COMMANDP[0]) return(SET_ERR(ERR_SYNTAX));
            platform_edit(COMMANDP);
            return(0);
		case 18:	/* SIV */
			return(do_si(1));
		case 19:	/* LIB */
			/* Session 2 fix, take 2: the first pass here still let
			   calldir(fbuf) fall through to do_calldir()'s own
			   defaults when fbuf had no directory component --
			   "." (cwd) and no extension filter -- instead of
			   $APLHOME/ws and *.ws. That's what was pulling in
			   unrelated files from cwd. Rewritten to match the
			   )FNSL case above exactly: always anchor to
			   $APLHOME/ws and always enforce WS_POSTFIX, only
			   adding the user's filt_build_pattern()'d text as an
			   extra name-prefix when they typed one. This gives up
			   the theoretical "point LIB at an arbitrary directory"
			   capability the old raw-pass-through code implied --
			   but that capability was already unreachable in
			   practice (see the CHAR_POW note from before), so
			   there was nothing real to preserve. */
			get_aplhome_dir((char *)LINEA, LINE_SIZE, "ws");
			if (*COMMANDP) {
				char fbuf[FILT_PAT_SIZE];
				filt_build_pattern((const char *)COMMANDP, fbuf, sizeof(fbuf));
				strcat(LINEA, fbuf);
			}
			strcat(LINEA,WS_POSTFIX);
			calldir((char *)LINEA);
			CNL();
			return(0);
		case 20:	/* PURGE */
			return(do_purge());
		case 21:	/* SYS -- removed V81: )SYS cmd was ⎕EXEC (same
				   callsystem()), bare )SYS listed the ⎕ names
				   (the Help window does that) */
		case 26:	/* HELP -- removed V81: help.dat, superseded by the
				   front end's Help menu */
		case 34:	/* SCRH -- removed V81: obsolete under Qt (the SCRH
				   variable stays; terminal.c uses it) */
		case 35:	/* HIST */
		case 36:	/* CASE -- removed V81: flipped the language tables
				   and broke the session (see set_case()) */
		case 37:	/* VERSION -- removed V81: the GUI's About box and
				   window title show the version */
			return(SET_ERR(ERR_COMMAND));
		case 22:	/* SI */
			return(do_si(0));
		case 23:	/* STACK */
			return(do_stack());
		case 24:	/* LIST */
			j = do_clist(COMMANDP);
			return(j);
		case 25:	/* OUTSYNC */
			Cmessage(MSG_WAS);
			print_num(OUTSYNC);
			if (OUTSYNC) OUTSYNC = 0;
			else OUTSYNC = 1;
			CNL();
			return(0);
		case 27:	/* BOOT */
			i = wspace->bootfn;
			wspace->bootfn = -1;
			if (*COMMANDP) {
				COMMANDP = get_vstr(COMMANDP);
				if (COMMANDP) {
					wspace->bootfn = lookup_sym(VBUF);
					if (wspace->bootfn < 0) {	/* table full (V108) */
						wspace->bootfn = i;
						return(SET_ERR(ERR_STOVF));
					}
				}
			}
			Cmessage(MSG_WAS);
			if (i != -1 ) print_name(i);
			else Cmessage(MSG_OFF);
			CNL();
			return(0);
		case 28:	/* PSNAP */
			prof_dump();
			prof_clear();
			return(0);
		case 29:	/* BASE */
			if (*COMMANDP) {
				i = strlen(COMMANDP);
				while (COMMANDP[i-1] == ' ') i--;
				COMMANDP[i] = '\0';
				i = OUT_BASE;
				if (!strcmp(COMMANDP,"hex")) OUT_BASE = 16;
				else OUT_BASE = 10;
				Cmessage(MSG_WAS);
			} else {
				i = OUT_BASE;
				Cmessage(MSG_IS);
			}	
			if (i == 16) Cmessage(MSG_HEX);
			else Cmessage(MSG_DEC);
			CNL();
			return(0);
		case 30:	/* stop */
		case 31:	/* trace */
			return(do_trace(cnum));
		case 32:	/* right */
			if (!print_stack(1)) PRINT("No right parm");
			NL();
			return(0);
		case 33:	/* left */
			if (!print_stack(0)) PRINT("No left parm");
			NL();
			return(0);
		case 39:    /* DROP */
        	return(do_drop());
		case 40:    /* RESIZE */
			l = 0L;
			if (*COMMANDP) sscanf((char *)COMMANDP,"%ld",&l);
			return(do_resize(l));
		case 41:	/* DIGITS */
			return(do_digits());
		case 42:	/* INTDIV */
			return(do_intdiv());
		case 43:	/* FONT */
			return(do_fonts(COMMANDP));
		case 44:	/* STEP */
			return(do_step());
		case 45:	/* IMAG */
			return(do_images(COMMANDP));
		case 46:	/* FSEARCH */
			return(do_fsearch(COMMANDP));
		case 47:	/* MIDI */
			return(do_midi(COMMANDP));
		case 48:	/* SEQUNIT (V102) */
			return(do_sequnit());
	}
	return(SET_ERR(ERR_COMMAND));
}

