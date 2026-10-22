// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	sysfuncs.c
*/

#define SYSFUNCS_C 1

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"
#include "externs.h"

/* WSVAL_PTR: typed wrapper for WS_PTR() where a ValEnt* is needed.
   WS_PTR() returns struct MemEnt*; this cast is needed for ValEnt field access. */
#define WSVAL_PTR(off) ((struct ValEnt *)WS_PTR(off))

extern char *sys_malloc();
extern short restart_mode;
extern void flush_output(void);
/* alt_input_proc() is already declared in externs.h (int alt_input_proc(UCHAR*,UCHAR*)) */


/* NOTE: this table CANNOT be more than 31 functions, as the input lines are processed into
a "sys-function" code followed by an 8 bit value indicating the function.  The 8 bit value
is split into 3 & 5 bits, the lower 5 is the table index and the upper 3 selects the command
table (in all_funcs[]). */

UCHAR sys_fnames[] = {
	'Z','Z',0,		/* dummy (resolves to a terminating null in the input text stream) */
	'T','S',0,		/* timestamp */
	'D','L',0,		/* delay */
	'I','N','T','D','I','V',0,	/* INTDIV - integer vs float dyadic divide, see )INTDIV.
				   Took over the unused TB (timebase) slot in place --
				   index 3 must not move, see SYSFUNCS_NOTES.md */
	'U','C','A','S','E',0,	/* UCASE (was CH -- dead/obsolete ledfuncs input-char stub, see quadnotes.txt) */
	'N','L',0,		/* NL char */
	'R','L',0,		/* RL - random link (generator state), get or set.
				   Took over the obsolete SCRH (screenheight) slot in
				   place -- index 6 must not move, see SYSFUNCS_NOTES.md.
				   The SCRH variable and )SCRH command are untouched. */
	'W','A',0,		/* free space */
	'W','I',0,		/* width */
	'V','T',0,		/* variable type */
	'C','Q',0,		/* convert quote */
	'A','R','G','S',0,	/* input args */
	'N','T','A',0,	/* numeric to alpha */
	'I','O',0,		/* IO, index origin )ORIGIN setting, case 13 */
	'E','N','V',0,	/* ENV */
	'F','O','N','T','H',0,	/* FONTH */
	'Z','T','W','I','D',0,	/* ZTWID - retired, was TWID. Renamed in place so
				   the name TWID could move to grfuncs.c (graph_fnames
				   slot 22, real text width); index 16 must not move,
				   see SYSFUNCS_NOTES.md */
	'S','Y','S',0,	/* special funcs */
	'S','I',0,		/* #SI */
	'A','T','N',0,	/* ATN */
	'S','T','R','U','C','T',0,
	'E','X','E','C',0,
	'W','E','X','E','C',0,
	'C','D',0,
	'L','C',0,		/* linecount */
	'X',0,
	'W','S','I','D',0,
	'C','T','R','L',0,
	'T','I','T','L','E',0,
	'A','V',0,
	'L','C','A','S','E',0,
	'D','I','G',0,	/* DIG - print-precision digits, see )DIGITS */
	0,0,0
};

UCHAR sys_ftypes[] = {
	NILAD,	/* 0 ZZ - Placeholder */
	NILAD,	/* 1 TS - time stamp */
	MONAD,	/* 2 DL - delay */
	MONAD_OPT,	/* 3 INTDIV - integer divide on/off, get or set (was TB) */
	MONAD,	/* 4 UCASE - convert to upper case (was CH, dead) */
	NILAD,	/* 5 NL */
	MONAD_OPT,	/* 6 RL - random link, get or set (was SCRH, NILAD) */
	NILAD,	/* 7 WA - watermark */
	MONAD_OPT,	/* 8 WI - width, get or set  */
	MONAD,	/* 9 VT - variable type */
	MONAD,	/* 10 CQ - convert quote */
	NILAD,	/* 11 ARGS */
	MONAD,	/* 12 NTA */
	MONAD_OPT,	/* 13 IO - index origin, get or set */
	MONAD,	/* 14 ENV */
	NILAD,	/* 15 FONTH */
	MONAD,	/* 16 ZTWID - retired (was TWID, see sys_fnames) */
	MONAD,	/* 17 sys */
	NILAD,	/* 18 si */
	MONAD,	/* 19 ATN */
	MONAD,	/* 20 STRUCT */
	MONAD,	/* 21 EXEC */
	MONAD,	/* 22 WEXEC */
	MONAD,	/* 23 CD  (change directory) */
	NILAD,	/* 24 LC - line count */
	MONAD,	/* 25 X (execute string) */
	NILAD,	/* 26 WSID */
	MONAD,	/* 27 CTRL */
	MONAD,	/* 28 TITLE */
	NILAD,	/* 29 AV (atomic vector) */
	MONAD,  /* 30 LCASE */
	MONAD_OPT,	/* 31 DIG */
	0
};

short
get_cargs()
{
short i,k;
REG short j;
char *cp;
struct ValEnt *te;

	k = 0;
	for (i=0;i<GLOBARGC;i++) 
	{
		j = strlen(GLOBARGV[i]);
		if (j > k) k = j;
	}
	te = make_temp(QUOT_CODE,2,(int32_t)(GLOBARGC * k));
	if (!te) return(SET_ERR(ERR_WSFULL));
	te->ve_rho[0] = (int32_t)k;
	te->ve_rho[1] = (int32_t)GLOBARGC;
	cp = DATA_AD(te);
	for (i=0;i<GLOBARGC;i++) 
	{
		for (j=0;j<k;j++) cp[j] = ' ';
		memcpy(cp,GLOBARGV[i],strlen(GLOBARGV[i]));	/* no NUL: fixed-width, space-padded row */
		cp += k;
	}
	return(0);
}

short
do_sysf(n,acc)
short n;
int32_t *acc;
{
	switch (n) {
		case 0:		/* version */
			*acc = 1;
			break;
		case 1:		/* abort )OFF */
			return(SET_ERR(ERR_KILL));
		case 2:		/* determine restart mode */
			*acc = (int32_t)restart_mode;
			break;
		case 4:		/* set APL_CASE for no translate */
			APL_CASE = 0;	
			break;
		case 5:		/* set APL_CASE for translate */
			APL_CASE = 1;
			break;
	}
	return(0);
}

/* return vector of pending fn lines */
short
do_sif()
{
int32_t *sp,l;
int32_t *lp;
short fline,count;
    
    lp = (int32_t *)LSCRATCH;
    lp[0] = (int32_t)FNLINE;
    count = 1;
    Adjust_Stack();
    sp = MSTACK;
    while (sp < STACKMAX && count < 100) 
    {
        sp++;
        l = *sp++;
        fline = (short)((l >> FLINE_SHIFT) & FLINE_MASK);
        if (fline) lp[count++] = (int32_t)fline;
        while (*sp != STACK_EMARK) 
        {
            if (*sp == STACK_TEMP_VAL) sp++;
            sp++;
        }
    }
    return(push_val(count,1,(int32_t*)lp));
}

short
sys_funcs(f,le,re)
short f;
struct ValEnt *le,*re;
{
struct ValEnt *ve;
int32_t *lp;
int32_t acc,l;
short i,j;
char *cp,*tp;
double *dpi,*dpo;
union MultiP rp;

	lp = LSCRATCH;
	acc = 0;
	switch (f) 
	{
		/* can't use 0, causes bug? */
		case 1:		/* TS - timestamp */
			get_tod();	/* get timestamp into larray */
			return(push_val(7,1,(int32_t*)lp));
		case 2:		/* DL - delay */
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(re,0);
			stack_pop(1);
			flush_output();
			time_delay(acc);
			break;
		case 3:		/* INTDIV - report or set integer dyadic divide, returns
				   prior value on set. 1 = integer divide (this APL's
				   original behavior), 0 = always float (classic APL,
				   and the default for new/cleared workspaces). Stored
				   inverted as wspace->floatdiv (0 = integer) so
				   workspaces saved before this setting existed read
				   as 1 -- see struct WorkSpace in parse.h. Same
				   setting as )INTDIV ON/OFF.
				   Replaces TB (timebase), which was unused; the
				   timebase variable itself is left alone for a
				   possible future fold into DL. */
			acc = wspace->floatdiv ? 0 : 1;
			if (re) {
				i = (short)get_data(re,0);
				if (i != 0 && i != 1) return(SET_ERR(ERR_DOMAIN));
				wspace->floatdiv = (UCHAR)(i ? 0 : 1);
				stack_pop(1);
			}
			break;
		case 4:		/* UCASE - convert to upper case */
		{
			short duped;

			if ((re->ve_vtype | VARIABLE_BIT) != QUOT_CODE) return(SET_ERR(ERR_DOMAIN));
			duped = !(re->ve_vtype & VARIABLE_BIT);
			if (duped) {
				re = dup_val(VRIGHT);
				if (!re) return(SET_ERR(ERR_WSFULL));
			}
			cp = DATA_AD(re);
			l = re->ve_length;
			while (l--) {
				if (*cp >= 'a' && *cp <= 'z') *cp -= 32;
				cp++;
			}
			if (duped) {
				stack_pop(1);
				temp_stack(re);
			}
			return(0);
		}
		case 5:	/* NL */
			acc = (int32_t)NL_CHAR;
			i = push_val(1,0,&acc);
			if (i) return(i);
			re = WSVAL_PTR(*VTEMP);
			re->ve_vtype = QUOT_CODE;	/* make char */
			return(0);	
		case 6:	/* RL - report or set the random link (the ? generator's
			   state), returns prior value on set. Replaces SCRH, whose
			   value was stale and mislabeled (see SYSFUNCS_NOTES.md);
			   the SCRH variable itself stays, )SCRH still uses it.
			   Same shape as IO/INTDIV: bare reports, an argument sets
			   and returns the prior value, stack_pop() only when re
			   is there (the MONAD_OPT rule, see PORTING_PLAN.md).
			   1..2147483646 sets the state exactly, so S←⎕RL ...
			   ⎕RL S replays from that point; 0 reseeds
			   unpredictably, as at startup. Anything else, a float,
			   or character data is ERR_DOMAIN, checked before
			   anything changes. Float rejected outright, as CTRL
			   does, rather than truncated: a seed that silently
			   isn't the number written defeats the point. The range
			   is checked in rl_set() (rndsrc.c), which owns the
			   modulus. Session-global, not saved in the workspace
			   -- decided in conversation, see rndsrc.c. */
			acc = rl_get();
			if (re) {
				if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
				if ((re->ve_vtype & BYTES_MASK) == FLOATSIZE)
					return(SET_ERR(ERR_DOMAIN));
				if ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE)
					return(SET_ERR(ERR_DOMAIN));
				if (!rl_set(get_data(re,0)))
					return(SET_ERR(ERR_DOMAIN));
				stack_pop(1);
			}
			break;
		case 7:	/* WA */
			acc = (int32_t)((char*)MSTACK - ((char*)wspace + wspace->water));
			acc -= STACK_MARGIN;
			break;
		case 8:	/* WI - report or set width, returns prior value on set */
			if (!re) {
				acc = (int32_t)WIDTH;
			} else {
				acc = (int32_t)WIDTH;
				i = (short)get_data(re,0);
				if (i < 20) i = 20;
				WIDTH = i;
				RMARGIN = LMARGIN + WIDTH;
				stack_pop(1);
			}
			break;
		case 9:	/* VT */
			if (!re) acc = 0;
			else acc = re->ve_vtype;
			stack_pop(1);
			break;
		case 10:	/* CQ */
			if (re->ve_vtype & VARIABLE_BIT) 	/* a temp? */
			{
				re->ve_vtype = CONST_CODE | (re->ve_vtype & BYTES_MASK);
			} else {
				le = dup_val(VRIGHT);
				if (!le) return(SET_ERR(ERR_WSFULL));
				le->ve_vtype = CONST_CODE | (le->ve_vtype & BYTES_MASK);
				stack_pop(1);
				temp_stack(le);
			}
			return(0);
		case 11:	/* ARGS */
			return(get_cargs());
		case 12:	/* NTA -- logic shared with the OP_FMT primitive (real Format),
				   see fmt_number() in monop.c */
			cp = (char *)lp;
			i = fmt_number(re,cp);
			if (i) return(i);
			stack_pop(1);
			return(push_str(cp));
		case 13:	/* IO - report or set index origin, returns prior value on set */
			if (!re) {
				acc = (int32_t)wspace->origin;
			} else {
				acc = (int32_t)wspace->origin;
				i = (short)get_data(re,0);
				if (i != 0 && i != 1) return(SET_ERR(ERR_DOMAIN));
				wspace->origin = i;
				stack_pop(1);
			}
			break;
		case 14:	/* ENV */
			cp = (char *)lp;
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			tp = DATA_AD(re);
			strncpy(cp,tp,i);
			cp[i] = '\0';
			get_mplenv(cp);
			stack_pop(1);
			return(push_str(cp));
		case 15:	/* FONTH */
			acc = (int32_t)FONTH;
			break;
		case 16:	/* ZTWID -- retired dead stub, formerly TWID. The
				   name TWID now belongs to the graphics module
				   (grfuncs.c case 22), which measures real text
				   width in a graphics window. Body left exactly as
				   it was, so a saved .ws whose compiled code still
				   references this slot behaves as before (returns
				   0). Now a spare slot for substitution-in-place. */
			cp = (char *)lp;
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			tp = DATA_AD(re);
			strncpy(cp,tp,i);
			cp[i] = '\0';
			stack_pop(1);
			acc = 0;	/* stringwidth() removed -- Qt handles font metrics */
			break;
		case 17:	/* vers */
			i = (short)get_data(re,0);
			stack_pop(1);
			i = do_sysf(i,&acc);
			if (i) return(i);	/* error */
			break;
		case 18:	/* #SI */	
			return(do_sif());
		case 19:	/* #ATN */
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			cp = DATA_AD(re);
			memcpy((char *)LINEA,cp,i);
			cp = (char *)LINEA;
			cp[i] = '\0';
			j = parsenums((UCHAR **)&cp,&i);
			stack_pop(1);
			/* return errors as null string */
			if (j < 1) return(push_val(0,1,(int32_t*)lp));
			if (j == 1) 
			{
				return(push_val(i,1,(int32_t*)lp));
			}
			i = i >> 1;	/* float length is double */
			ve = make_temp(FCONST_CODE,1,(int32_t)i);
			if (!ve) return(SET_ERR(ERR_WSFULL));
			dpi = (double *)LSCRATCH;
			dpo = (double *)DATA_AD(ve);
			while (i--) *dpo++ = *dpi++;
			return(0);
		case 20:	/* struct */
			j = (short)SBOUNDS(re);		/* get rank */
			if (j > MAX_RANK) return(SET_ERR(ERR_RANK));
			acc = 1;
			for (i=0;i<j;i++) 
			{
				l = get_data(re,i);
				acc *= l;	/* accumulate total size */
				*lp++ = l;
			}
			stack_pop(1);
			ve = make_temp(STRUCT_CODE,j,acc);
			if (!ve) return(SET_ERR(ERR_WSFULL));
			for (i=0;i<j;i++) ve->ve_rho[i] = *--lp;
			rp.cp = DATA_AD(ve);
			while (acc--) *rp.lp++ = 0;
			return(0);
		case 21:	/* EXEC (asynchronous exec) */
			cp = (char *)lp;
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			tp = DATA_AD(re);
			strncpy(cp,tp,i);
			cp[i] = '\0';
			acc = (int32_t)callsystem(cp);
			stack_pop(1);
			break;
		case 22:	/* WEXEC (wait till done exec) */
			cp = (char *)lp;
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			tp = DATA_AD(re);
			strncpy(cp,tp,i);
			cp[i] = '\0';
			acc = (int32_t)waitsystem(cp);
			stack_pop(1);
			break;
		case 23:	/* CD (change dir/get current dir) */
			cp = (char *)lp;
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			tp = DATA_AD(re);
			strncpy(cp,tp,i);
			cp[i] = '\0';
			cp = perform_cd(cp);
			stack_pop(1);
			return(push_str(cp));
		case 24:	/* LC - linecount */
			acc = (int32_t)FNLINE;
			break;
		case 25:	/* X (execute string) -- logic shared with the OP_QX primitive
				   (real Execute), see prep_exec_line() in monop.c */
			prep_exec_line(re);
			stack_pop(1);		/* remove the input string param */
			return(exec_lineb());

		case 26:	/* WSID - report current workspace name */
			return(push_str(wspace->wsid));

		case 27:	/* CTRL, subfunc,mode ⎕CTRL. re[0] selects
				   which control sub-function; the rest of
				   re is that sub-function's own
				   parameter(s) -- re is a dispatch vector,
				   not a single mode value. Currently only
				   sub-function 1 ("window sizing") exists:
				   re = 1,mode where mode is 0 = minimize,
				   1 = restore, 2 = maximize, applied to the
				   main terminal window (no window number
				   involved -- there's only the one terminal;
				   see apl_graphics_other_ideas_notes_v1.md's
				   CTRL section for why this and the graphics
				   module's WCTRL are two separate functions
				   rather than one merged one). Any other
				   sub-function number is ERR_DOMAIN, not a
				   silent no-op -- nothing else is designed
				   yet, so pretending to accept it would be
				   misleading rather than genuinely
				   forward-compatible. Whatever future
				   sub-functions look like, and how many of
				   their own parameters each takes beyond
				   re[1], is still TBD.
				   Requires at least 2 elements (subfunc,mode)
				   -- ERR_LENGTH otherwise. Fire-and-forget via
				   sys_ctrl() (main.cpp) for the window-sizing
				   sub-function -- no result the engine thread
				   needs back, same category as the graphics
				   module's own window-state-only calls.
				   Single Qt6 implementation underneath, no
				   platform-specific file -- showMinimized()/
				   showNormal()/showMaximized() are the same
				   call on every platform Qt6 targets here. */
			if (SBOUNDS(re) < 2) return(SET_ERR(ERR_LENGTH));
			if ((re->ve_vtype & BYTES_MASK) == FLOATSIZE) return(SET_ERR(ERR_DOMAIN));
			acc = get_data(re,0);		/* sub-function selector */
			if (acc != 1) return(SET_ERR(ERR_DOMAIN));
					/* only window sizing exists so far */
			acc = get_data(re,1);		/* mode for sub-function 1 */
			if (acc < 0 || acc > 2) return(SET_ERR(ERR_DOMAIN));
			stack_pop(1);
			sys_ctrl(acc);
			acc = 0;
			break;
		case 28:	/* TITLE */
			cp = (char *)lp;
			i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
			tp = DATA_AD(re);

			strncpy(cp,tp,i);
			cp[i] = '\0';

			/* sys_title() -- window title handled by Qt frontend */
			/* Qt frontend can implement set_window_title() if needed */
			return(0);
		case 29:	/* AV - atomic vector */
			ve = make_temp(QUOT_CODE,1,256);
			tp = DATA_AD(ve);
			for (i=0;i<256;i++) *tp++ = (char)i;
			return(0);
		case 30:	/* LCASE to lower case */
		{
			short duped;

			if ((re->ve_vtype | VARIABLE_BIT) != QUOT_CODE) return(SET_ERR(ERR_DOMAIN));
			duped = !(re->ve_vtype & VARIABLE_BIT);
			if (duped) {
				re = dup_val(VRIGHT);
				if (!re) return(SET_ERR(ERR_WSFULL));
			}
			cp = DATA_AD(re);
			l = re->ve_length;
			while (l--) {
				if (*cp >= 'A' && *cp <= 'Z') *cp += 32;
				cp++;
			}
			if (duped) {
				stack_pop(1);
				temp_stack(re);
			}
			return(0);
		}
		case 31:	/* DIG - report or set )DIGITS precision, returns prior value on set */
			if (!re) {
				acc = (int32_t)DIGITS;
			} else {
				acc = (int32_t)DIGITS;
				i = (short)get_data(re,0);
				/* DIGITS>0: fixed decimals, trailing zeros kept.
				   DIGITS<0: up to |DIGITS| decimals, trailing zeros
				   trimmed (see floatlen() in vprint.c). 0 is valid
				   either way -- matches the )DIGITS command's clamp
				   in commands.c. */
				if (i > 17) i = 17;
				if (i < -17) i = -17;
				DIGITS = i;
				stack_pop(1);
			}
			break;
		case CLEANUP_FUNC:
			return(0);
		default:
			return(SET_ERR(ERR_SYSTEM));
	}
	return(push_val(1,0,&acc));
}

