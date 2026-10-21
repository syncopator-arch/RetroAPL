/*
	fn.c
*/

#define FN_C 1

#include "parse.h"
#include "mplmem.h"
#include "externs.h"

extern ws_off_t *get_vparm();
extern struct ValEnt *dup_val(ws_off_t *);
extern short line_start_trace(UCHAR flags);	/* exec.c */

/*
 * global_save() -- save a symbol table entry onto the machine stack.
 *
 * SYMT[r].value is now ws_off_t (a workspace offset), not a pointer.
 * We push it as an int32_t.  We also push the type byte as an int32_t.
 * If the entry has a live ValEnt we update its ve_reference to point at
 * the stack slot we just pushed the offset into, so GarbCollect can
 * track and adjust it.
 */
short
global_save(short r)
{
struct ValEnt *ve;
ws_off_t voff;

	if (r == -1) return(0);
	voff = SYMT[r].value;				/* ws_off_t offset (0 = no value) */
	ve   = voff ? (struct ValEnt *)WS_PTR(voff) : (struct ValEnt *)0;

	*--MSTACK = (int32_t)voff;			/* push offset onto stack */
	if (ve) 
	{
		ve->ve_reference = WS_OFF(MSTACK);	/* tell GC where it lives */
		debug_check_ref("fn.c/global_save", (struct MemEnt*)ve, ve->ve_reference); /* %%% DBG */
	}

	*--MSTACK = (int32_t)SYMT[r].type;		/* remember type */
	SYMT[r].value = 0;				/* now nonexistent */
	SYMT[r].type  = SYM_UNDEF;			/* and unknown type */
	return(1);
}

/*
 * global_restore() -- restore a symbol table entry from the machine stack.
 */
short
global_restore(short r)
{
struct ValEnt *ve;
ws_off_t voff;

	if (r == -1) return(0);			/* none to restore */
	SYMT[r].type = (UCHAR)*MSTACK;		/* restore type */
	*MSTACK++ = 0;
	voff = (ws_off_t)*MSTACK;		/* pop the ws offset */
	*MSTACK++ = 0;				/* must zero to ensure delete */
	ve = voff ? (struct ValEnt *)WS_PTR(voff) : (struct ValEnt *)0;
	if (ve) 
	{
		ve->ve_reference = WS_OFF(&SYMT[r].value);
		debug_check_ref("fn.c/global_restore", (struct MemEnt*)ve, ve->ve_reference); /* %%% DBG */
	}
	SYMT[r].value = voff;
	return(1);
}

short
pop_fn()
{
short r,locs,j;
int32_t l;
struct ValEnt *ve;
struct Func0Ent *rf;

	Adjust_Stack();		/* restore stack to reasonable point */
	if (MSTACK >= STACKMAX)
	{
		ERROR_CODE = SET_ERR(ERR_SYSTEM); /* system error, nothing on stack */
		return(1);
	}

	*MSTACK++ = 0;				/* remove emark */
	l = *MSTACK;				/* get fn line entry */
	*MSTACK++ = 0;
	FNLINE = (short)(l >> FLINE_SHIFT) & FLINE_MASK;
	j      = (short)((l >> LVAR_SHIFT) & LVAR_MASK);	/* stacked locals */
	ILEN   = (short)((l >> ILEN_SHIFT) & ILEN_MASK);
	l = *MSTACK;				/* get fnhead/CURFUNC */
	*MSTACK++ = 0;

	if (FUNC_NUM != -1)
	{	
		/* popping out of a func? */
		rf = (struct Func0Ent *)WS_PTR(SYMT[FUNC_NUM].value);
		r  = rf->f0e_result;

//		if (r != -1) ve = (struct ValEnt *)WS_PTR(SYMT[r].value);	/* save this one */
//		else         ve = (struct ValEnt *)0;

		if (r != -1) 
			ve = SYMT[r].value ? (struct ValEnt *)WS_PTR(SYMT[r].value) : (struct ValEnt *)0;
		else         ve = (struct ValEnt *)0;

		locs = rf->f0e_MemEnt.me_rank;
		while (locs && j) j -= global_restore(rf->f0e_locals[--locs]);
		if (j) j -= global_restore(rf->f0e_left);
		if (j) j -= global_restore(rf->f0e_right);
		if (r != -1)
		{
			if (j) global_restore(r);	/* restore the global */
			if (ve) temp_stack(ve);		/* fn returns val */
		}
	}

	if (FNLINE)		/* popping to a func */
	{
		FUNC_NUM = (short)((l >> FNUM_SHIFT) & FNUM_MASK);
		NEXTLINE = FNLINE + 1;
	} else {
		FUNC_NUM = -1;
		wspace->curfunc = (ws_off_t)l;
	}
	return(0);
}

/*
 * var_asn() -- assign a ValEnt to a symbol table slot.
 *
 * SYMT[r].value is ws_off_t so we store WS_OFF(ve).
 * ve->ve_reference is also ws_off_t: store WS_OFF of the slot.
 */
void
var_asn(short r, struct ValEnt *ve)
{
	if (r == -1) return;			/* none to do */
	SYMT[r].value        = WS_OFF(ve);
	ve->ve_reference     = WS_OFF(&SYMT[r].value);
	ve->ve_vtype        &= ~VARIABLE_BIT;
}

/*
 * state_stack() -- push current execution context onto the machine stack.
 * pop_fn() is the matching pop.
 *
 * CURFUNC is a live pointer (special case -- not a ws_off_t) so it is
 * pushed directly as an intptr_t cast to int32_t.  This matches the
 * original semantics; the pointer is only valid within a single session
 * and is never serialised to disk.
 *
 * CURFUNC->fe_MemEnt.me_reference is a ws_off_t that must track where
 * the CURFUNC pointer lives so GarbCollect does not try to adjust it.
 * We point it at the stack slot we just pushed.
 */
void
state_stack(short fnum, int locvars)
{
int i;

	/* stack where we came from */

	i = ILEN;
	if (i < 0) i = 0;
	if (FUNC_NUM == -1)		/* at command line, stack CURFUNC */
	{
		*--MSTACK = (int32_t)wspace->curfunc;
		if (wspace->curfunc) CURFUNC->fe_MemEnt.me_reference = WS_OFF(MSTACK);
		*--MSTACK = (int32_t)(STACK_FLINE | ((int32_t)i << ILEN_SHIFT) | ((int32_t)locvars << LVAR_SHIFT));
		wspace->curfunc = 0;
	} else {
		/* within another fn, stack it */
		*--MSTACK = (int32_t)(STACK_FNHEAD | FUNC_NUM);
		*--MSTACK = (int32_t)(STACK_FLINE | ((int32_t)FNLINE << FLINE_SHIFT) | ((int32_t)i << ILEN_SHIFT) | ((int32_t)locvars << LVAR_SHIFT));
	}
	*--MSTACK = (int32_t)STACK_EMARK;

	FUNC_NUM = fnum;

	if (fnum >= 0)
	{
		FNLINE   = 1;		/* where we expect to be now */
		NEXTLINE = 2;		/* next */
	} else {
		FNLINE   = -1;		/* we're not in a fn */
		NEXTLINE = 0;
	}
	LAST_OPERATOR = -1;
}

/*
 * frame_fits() -- can the current place in the line be saved?
 *
 * state_stack() and error_push() keep "compiled bytes left on the line"
 * (ILEN) in 8 bits of the frame word (ILEN_MASK), and 255 means "start
 * of line" to end_fn(), so a count over 254 can't be saved: it would
 * spill into the line-number bits and the caller would resume at the
 * wrong place (seen as: the called function runs, then a Syntax Error).
 * Lines can be that long since LINE_SIZE grew past 257. It's rare, so
 * rather than widen the frame (see PORTING_PLAN.md Backlog), a call or
 * execute made from such a place is refused before anything changes:
 * a line saying what and why, then a Length Error. `what` is the
 * function's symbol number, or -1 for execute/quad input.
 */
short
frame_fits(short what)
{
char cb[80];

	if (ILEN <= 254) return(1);
	if (CURSX) NL();
	if (what >= 0) print_name(what);
	else PRINT("Execute or quad input");
	snprintf(cb, sizeof(cb), ": called with %d compiled bytes left on the line (limit 254)", (int)ILEN);
	PRINT(cb);
	NL();
	ERROR_CODE = SET_ERR(ERR_LENGTH);
	return(0);
}

void
begin_fn(short fnum)
{
short i,j;
struct Func0Ent *rf;
struct FuncEnt  *fe;
static struct ValEnt *rv, *lv;
intptr_t m;
ws_off_t fnum_off;

    fnum_off = SYMT[fnum].value;
    rf = (struct Func0Ent *)WS_PTR(fnum_off);
    if (!rf->f0e_Line1) return;         /* no lines in program */
    if (!frame_fits(fnum)) return;      /* caller's place can't be saved */

    m  = (intptr_t)wspace + STACK_MARGIN + rf->f0e_MemEnt.me_rank * 8;
    m += wspace->water;

    if (m >= (intptr_t)MSTACK)
    {
        GarbCollect();
        rf = (struct Func0Ent *)WS_PTR(SYMT[fnum].value);
        if (m >= (intptr_t)MSTACK)
        {
            while (GarbCollect())
                rf = (struct Func0Ent *)WS_PTR(SYMT[fnum].value);
            if (m >= (intptr_t)MSTACK)
            {
                ERROR_CODE = SET_ERR(ERR_WSFULL);
                return;
            }
        }
    }

    rf = (struct Func0Ent *)WS_PTR(SYMT[fnum].value);

    if (rf->f0e_left != -1)
    {
		ERROR_CODE = SET_ERR(ERR_SYNTAX);
        VLEFT = get_vparm(0);
        if ((intptr_t)VLEFT < 1L) goto bfn_exit;
        if (!*VLEFT) { ERROR_CODE = SET_ERR(ERR_VALUE); goto bfn_exit; }
        lv = (struct ValEnt *)WS_PTR(*VLEFT);
        ERROR_CODE = 0;

        if (*MSTACK != STACK_TEMP_VAL)
        {
            lv = dup_val(VLEFT);        /* make a temp dup */
            if (!lv) { ERROR_CODE = SET_ERR(ERR_WSFULL); goto bfn_exit; }
        }
        /* anchor lv via tmp_me, NOT tmp_ve: the right argument's
           dup_val() below goes through dup_sval(), which uses tmp_ve
           as its own anchor -- MakeEnt() writes the new copy's offset
           over it and dup_sval() then zeroes it. With lv anchored there
           (until V103), a GC inside that MakeEnt() (every allocation,
           with )GARB on) moved lv, the anchor was then lost, and lv
           was never re-derived: M got a stale pointer into the right
           argument's copy, and ⍴M read B's data as a header and ran
           off the end of LSCRATCH. Seen as (12+7 4 0) F B -- a left
           temp with garbage below it, a right argument that needs
           copying. Only fn.c uses tmp_me, so nothing in dup_val()
           can touch it. */
        wspace->tmp_me = WS_OFF(lv);
        lv->ve_reference = WS_OFF(&wspace->tmp_me);
		debug_check_ref("fn.c/lv_floatv", (struct MemEnt*)lv, lv->ve_reference); /* %%% DBG */
        VLEFT = &wspace->tmp_me;
        stack_pop(1);
        rf = (struct Func0Ent *)WS_PTR(SYMT[fnum].value);
        lv = (struct ValEnt *)WS_PTR(wspace->tmp_me);  /* re-derive after pop */
    } else {
        VLEFT = (ws_off_t *)0;
        lv    = (struct ValEnt *)0;
    }

    if (rf->f0e_right != -1)
    {
		ERROR_CODE = SET_ERR(ERR_SYNTAX);
        VRIGHT = get_vparm(0);
        if ((intptr_t)VRIGHT < 1L) goto bfn_exit;
        if (!*VRIGHT) { ERROR_CODE = SET_ERR(ERR_VALUE); goto bfn_exit; }
        rv = (struct ValEnt *)WS_PTR(*VRIGHT);
        ERROR_CODE = 0;

        if (*MSTACK != STACK_TEMP_VAL)
        {
            rv = dup_val(VRIGHT);       /* make a temp dup */
            if (!rv) { ERROR_CODE = SET_ERR(ERR_WSFULL); goto bfn_exit; }
        }
        /* anchor rv via tmp_ve -- safe here: dup_val() is done, and
           dup_sval() left tmp_ve zero; nothing below allocates */
        wspace->tmp_ve = WS_OFF(rv);
        rv->ve_reference = WS_OFF(&wspace->tmp_ve);
		debug_check_ref("fn.c/rv_floatv", (struct MemEnt*)rv, rv->ve_reference); /* %%% DBG */
        VRIGHT = &wspace->tmp_ve;
        stack_pop(1);
        rf = (struct Func0Ent *)WS_PTR(SYMT[fnum].value);
        rv = (struct ValEnt *)WS_PTR(wspace->tmp_ve);  /* re-derive after pop */
        /* re-derive lv too if we have one -- rv's dup_val may have
           compacted the pool, and GarbCollect() kept tmp_me current */
        if (wspace->tmp_me)
            lv = (struct ValEnt *)WS_PTR(wspace->tmp_me);
    }

    j  = global_save(rf->f0e_result);
    j += global_save(rf->f0e_right);
    j += global_save(rf->f0e_left);
    j += rf->f0e_MemEnt.me_rank;

    i = 0;
    while (i < rf->f0e_MemEnt.me_rank) global_save(rf->f0e_locals[i++]);

    if (ERROR_CODE) goto bfn_exit;

    /* var_asn transfers ownership to SYMT slots -- clear anchors after */
    var_asn(rf->f0e_right, rv);
    var_asn(rf->f0e_left,  lv);
    wspace->tmp_ve = 0;
    wspace->tmp_me = 0;

    state_stack(fnum, j);

    fe = (struct FuncEnt *)WS_PTR(rf->f0e_Line1);
    exec_setup(fe->fe_exec, 0);
    TRACEFLAG = line_start_trace(fe->fe_MemEnt.me_flags);
    debug_report();
    LAST_OPERATOR = -1;

bfn_exit:;
    wspace->tmp_ve = 0;
    wspace->tmp_me = 0;
    rv = 0;
    lv = 0;
}

void
end_fn()
{
short fline, ilb, i, fromprompt;
struct FuncEnt *fe;

	fromprompt = (FUNC_NUM == -1);	/* ->0 resuming (or exec_lineb()) */
	if (pop_fn()) return;

	/* ->0 resuming a function at the start of its line -- after an
	   error, a )STOP, or a position too big to save (see error_push()):
	   drop whatever the earlier attempt left on the stack above the
	   function's own frame, so the rerun starts clean. A return from a
	   call never gets here: its frame always holds a real position
	   (frame_fits()), and pop_fn() has just pushed the result. */
	if (fromprompt && FUNC_NUM != -1 && ILEN == 255) Adjust_Stack();

	if (FUNC_NUM != -1)		/* from another fn */
	{
		fe    = (struct FuncEnt *)WS_PTR(SYMT[FUNC_NUM].value);
		fline = 0;
		while (fline < FNLINE)		/* find current line */
		{
			if (!fe->fe_NextLine)
			{
				/* no such line: ->N at the prompt went past the
				   end (see resume_line()) -- leave the function,
				   as do_exec() does for a branch past the end */
				end_fn();
				return;
			}
			fe = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
			fline++;
		}
		/* ME_FTRACE only: this continues a line already under way
		   (a resume, or a return from a call), and )STOP stops only
		   where a line starts -- see lexec() */
		TRACEFLAG = fe->fe_MemEnt.me_flags & ME_FTRACE;
	} else {		/* from command line */
		CURFUNC->fe_MemEnt.me_reference = (ws_off_t)((char*)&wspace->curfunc - (char*)wspace);
		fe = CURFUNC;
	}
	IP = fe->fe_exec;
	i  = *IP++ << 8;
	i += *IP++;
	i -= 2;
	if (ILEN == 255) ILEN = i;		/* starting a new line */
	ilb = i - ILEN;
	exec_setup(fe->fe_exec, ilb);
	LAST_OPERATOR = -1;
}

/*
 * resume_line() -- ->N (N > 0) at the prompt, from monop.c's OP_BRA:
 * point the innermost suspended function's frame at the start of line n,
 * so the end_fn() that follows resumes there rather than where it
 * stopped (and, ILEN being 255, clears what was left on the stack).
 * Returns 0, changing nothing, if the top frame isn't a suspended
 * function. A line past the end is left to end_fn(), which leaves the
 * function; n is capped at FLINE_MASK so it still reads back as past
 * the end.
 */
short
resume_line(int32_t n)
{
int32_t *sp;

	Adjust_Stack();
	sp = MSTACK;
	if (sp + 2 >= STACKMAX || *sp != STACK_EMARK) return(0);
	if ((sp[1] & 0xf0000000) != STACK_FLINE) return(0);
	if ((sp[2] & 0xf0000000) != STACK_FNHEAD) return(0);
	if (n > FLINE_MASK) n = FLINE_MASK;
	sp[1] = (sp[1] & ((int32_t)LVAR_MASK << LVAR_SHIFT))
	      | (int32_t)STACK_FLINE | (n << FLINE_SHIFT) | 255;
	return(1);
}

void
error_push()
{
short i;

	i = ILEN;	/* don't subtract one here... */
	if (i < 0) i = 0;
	if (i > 254) i = 255;	/* can't be saved in 8 bits (see frame_fits()):
				   record "start of line", so the line number
				   stays right and ->0 reruns the line */
	/* After a real error, ->0 reruns the line from its start rather
	   than carrying on past the operation that failed, which may have
	   left an unfinished result behind (3/0 did). An interrupt, or a
	   )STEP OP stop (ERROR_CODE still 0 then), resumes exactly. */
	if (ERROR_CODE && ERROR_CODE != SET_ERR(ERR_INTERRUPT)) i = 255;
	if (NEXTLINE != (FNLINE + 1)) {		/* branched? */
		FNLINE = NEXTLINE;
		i = 255;
	}
	*--MSTACK = (int32_t)(STACK_FNHEAD | FUNC_NUM);
	*--MSTACK = (int32_t)(STACK_FLINE  | ((int32_t)FNLINE << FLINE_SHIFT) | i);
	*--MSTACK = (int32_t)STACK_EMARK;
}
