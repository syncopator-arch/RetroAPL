/*
	exec.c
	1.2 - remove debug line
*/

#define EXEC_C 1

#define PROFILING

#include "parse.h"
#include "mplmem.h"
#include "externs.h"
#include "platform.h"

extern short do_dyop();
extern short do_monop();
extern short do_iprod();	/* generalized inner product, Stage 5 (not yet implemented) */
extern short do_index();
extern short do_command();
extern short input_proc();
extern UCHAR op_table[];		/* read only */
extern UCHAR char_xtable[];		/* ditto */
extern void get_aplhome_dir(char *out, size_t outsz, const char *subdir);

short exec_lineb();	/* predefine for proc_quad */
void end_fn();
short pop_fn();			/* fn.c */
short frame_fits(short what);	/* fn.c */

int enable_line_events = 0;
int step_mode = STEP_OFF;	/* )STEP: STEP_OFF, STEP_LINE or STEP_OP; a
				   session setting, not saved with the ws */
static int lexec_depth = 0;	/* lexec() nesting: 1 for a line typed or
				   run from a function, more inside execute
				   or quad input (exec_lineb()) */
int step_resumed = 0;		/* )STEP applies only while running what a
				   ->0 resumed: set in do_monop()'s OP_BRA
				   when ->0 at the prompt resumes a function,
				   cleared when lexec() starts a typed line */

short bsizetab[] = {
	0,0,1,0,2,0,0,0,3
};

#ifdef PROFILING
long profile_table[256];
#endif

void
prof_clear()
{
#ifdef PROFILING
short i;
	for (i=0;i<256;i++) profile_table[i] = 0L;
#endif
}

void
prof_dump()
{
#ifdef PROFILING
FILE *fp;
char path[PATHBUF_SIZE];

	get_aplhome_dir(path, sizeof(path), "");
	strcat(path,"profiles");
	fp = fopen(path,"w");
	if (fp) {
		fwrite((char *)profile_table,4,256,fp);
		fclose(fp);
	}
#endif
}

long
compsize(bsize,rank,count)
short bsize;
short rank;
long count;
{
long len;

	bsize &= BYTES_MASK;
	if (count > (0x7fffffffL >> bsizetab[bsize])) return(0x7fffffffL);
	len = count << bsizetab[bsize];
	len += (long)((rank << 2) - 4);	/* length of rho */
	len += sizeof(struct ValEnt) - sizeof(struct MemEnt);
	return(len);
}

/* 
	PERFORMANCE:  this routine gets called lots, tuneup will
	increase performance.

	note:  if count is 1, result will be scalar 
*/
struct ValEnt *
make_temp(typ, rank, count)
short typ;
short rank;
long count;
{
long len;
struct ValEnt *ve;

    len = compsize(typ, rank, count);

    MSTACK--;                           /* place for entry offset on stack */
    VTEMP = (ws_off_t *)MSTACK;        /* VTEMP -> offset slot */

    ve = (struct ValEnt *)MakeEnt(len, ME_VAL, VTEMP);
    /* MakeEnt: *VTEMP = WS_OFF(ve), ve->me_reference = WS_OFF(VTEMP) */

    if (!ve) {
        MSTACK++;                       /* undo the single decrement */
        return (struct ValEnt *)0;
    }

    *--MSTACK = STACK_TEMP_VAL;        /* sentinel pushed after, ends up at lower address */

    ve->ve_vtype  = typ | VARIABLE_BIT;
    ve->ve_rank   = rank;
    ve->ve_length = count;
    ve->ve_point  = (int32_t)((char *)CDATA_AD(ve) - (char *)ve);
    if (rank == 1) ve->ve_rho[0] = count;
    return(ve);
}

short
do_byte()
{
struct ValEnt *ve;
REG short i,count;
REG char *bp;

	count = (short)*IP++;
	i = 1;	/* rank */
	if (count == 1) i = 0;
	ve = make_temp(BCONST_CODE,i,(long)count);
	ILEN--;
	if (!ve) {
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return(-1);	/* out of memory */
	}
	bp = (char *)&ve->ve_rho[0];
	if (ve->ve_rank) {
		bp += 4;
		count = (short)ve->ve_rho[0];
	} else count = 1;
	for (i=0;i<count;i++) {
		*bp++ = *IP++;		
		ILEN--;
	}
	return(0);	/* ok exit */
}

short
do_quote()
{
struct ValEnt *ve;
REG short i,count;
REG char *bp;

	count = (short)*IP++;   
	ILEN--;		/* count it */
	i = 1;
	if (count == 1) i = 0;
	ve = make_temp(QUOT_CODE,i,(long)count);
	if (!ve) {
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return(-1);		/* out of memory */
	}
	bp = (char *)&ve->ve_rho[0];
	if (ve->ve_rank) {
		bp += 4;
		count = (short)ve->ve_rho[0];
	} else count = 1;
	for (i=0;i<count;i++) {		/* move string into entry */
		*bp++ = *IP++;		
		ILEN--;
	}
	return(0);	/* ok exit */
}

short
do_short()
{
struct ValEnt *ve;
REG short i,acc;
REG short *sp;
short count;

	count = (short)*IP++;
	i = 1;
	if (count == 1) i = 0;
	ve = make_temp(SCONST_CODE,i,(long)count);
	ILEN--;
	if (!ve) {
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return(-1);	/* out of memory */
	}
	sp = (short *)&ve->ve_rho[0];
	if (ve->ve_rank) {
		sp += 2;
		count = (short)ve->ve_rho[0];
	} else count = 1;
	for (i=0;i<count;i++) {
		acc = (*IP++) << 8;
		*sp++ = acc + *IP++;
		ILEN -= 2;
	}
	return(0);	/* ok exit */
}

short
do_long()
{
struct ValEnt *ve;
REG short i;
REG int32_t acc;
REG int32_t *lp;
short count;

	count = (short)*IP++;
	i = 1;
	if (count == 1) i = 0;
	ve = make_temp(ICONST_CODE,i,(long)count);
	ILEN--;
	if (!ve) {
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return(-1);	/* out of memory */
	}
	lp = (int32_t *)&ve->ve_rho[0];
	if (ve->ve_rank) {
		lp += 1;
		count = (short)ve->ve_rho[0];
	} else count = 1;
	for (i=0;i<count;i++) {
		/* Assembled unsigned, converted once (V75): shifting a top
		   byte of 128 or more left 24 places in a signed int is
		   undefined (UBSan, on -2000000000's 0x88), though it gave
		   the right bits. */
		uint32_t u;
		u  = (uint32_t)*IP++ << 24;
		u |= (uint32_t)*IP++ << 16;
		u |= (uint32_t)*IP++ << 8;
		u |= (uint32_t)*IP++;
		acc = (int32_t)u;
		*lp++ = acc;
		ILEN -= 4;
	}
	return(0);	/* ok exit */
}

short
do_float()
{
struct ValEnt *ve;
REG short i,j;
static double dacc;
REG int32_t *lp;
REG double *dp;
REG char *accp;
short count;

	count = (short)*IP++;
	i = 1;
	if (count == 1) i = 0;
	ve = make_temp(FCONST_CODE,i,(long)count);
	ILEN--;
	if (!ve) {
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return(-1);	/* out of memory */
	}
	lp = (int32_t *)&ve->ve_rho[0];
	if (ve->ve_rank) {
		lp += 1;
		count = (short)ve->ve_rho[0];
	} else count = 1;
	dp = (double *)lp;
	accp = (char *)&dacc;
	for (i=0;i<count;i++) {
		for (j=0;j<FLOATSIZE;j++) accp[j] = *IP++;
		*dp++ = dacc;
		ILEN -= FLOATSIZE;
	}
	return(0);	/* ok exit */
}

struct ValEnt *
dup_sval(ws_off_t *ve)
{
long sz;
struct ValEnt *re;
struct ValEnt *src;

	if (!ve || !*ve) return((struct ValEnt *)0L);
    src = (struct ValEnt *)WS_PTR(*ve);
	debug_check_ref("dup_sval/source", (struct MemEnt*)src, src->ve_reference); /* DBG %%% */
    sz = src->ve_size - sizeof(struct MemEnt);
    re = (struct ValEnt *)MakeEnt(sz, ME_VAL, &wspace->tmp_ve);
    if (re) {
        src = (struct ValEnt *)WS_PTR(*ve);  /* re-derive after MakeEnt/GarbCollect */
		debug_check_ref("dup_sval/source_after_makeent", (struct MemEnt*)src, src->ve_reference); /* DBG %%% */
        memcpy((char *)re, (char *)src, re->ve_size);
        re->ve_vtype |= VARIABLE_BIT;
        re->ve_reference = WS_OFF(&wspace->tmp_ve);
        wspace->tmp_ve = WS_OFF(re);
    }
    wspace->tmp_ve = 0;
    return(re);
}

void
temp_stack(te)
struct ValEnt *te;
{
	*--MSTACK = WS_OFF(te);
	te->ve_reference = WS_OFF(MSTACK);
	debug_check_ref("exec.c/temp_stack", (struct MemEnt*)te, te->ve_reference); /* %%% DBG */
	*--MSTACK = STACK_TEMP_VAL;
}

struct ValEnt *
dup_val(ws_off_t *ve)
{
long sz,ind;
int i;
static struct ValEnt *re,*te;
static union MultiP vp;

	if (!ve) return((struct ValEnt *)0L);
	re = dup_sval(ve);
	if (!re) return(re);
	if ((re->ve_vtype | VARIABLE_BIT) != STRUCT_CODE) return(re);

	temp_stack(re);		/* anchor the dest */

	sz = ((struct ValEnt *)WS_PTR(*ve))->ve_length;

	/* get a floater */
	i = AllocFloatv();
	if (i == -1) return((struct ValEnt *)0L);

	for (ind=0;ind<sz;ind++) 
	{
		vp.cp = DATA_AD((struct ValEnt *)WS_PTR(*ve));	/* point to source */

		/* nested struct pointers stored as ws_off_t in data area */
		te = (struct ValEnt *)WS_PTR(*(vp.lp + ind));
		if (te) 
		{
			FLOATVS[i] = WS_OFF(te);
			te = dup_val(&FLOATVS[i]);	/* recurse via ws_off_t slot */
			if (!te) return(te);
		}	

		re = (struct ValEnt *)WS_PTR(MSTACK[1]);	/* find dest */
		vp.cp = DATA_AD(re);
		vp.lp += ind;
		*vp.lp = (int32_t)(te ? WS_OFF(te) : 0);	/* link it */
		if (te) 
		{
			te->ve_reference = WS_OFF(vp.lp);
			debug_check_ref("exec.c/vp.lp", (struct MemEnt*)te, te->ve_reference); /* %%% DBG */
		}

	}
	MSTACK += 2;	/* pop the dest */
	FLOATVS[i] = 0;	/* free the floater */
	return(re);
}

/*
	PERFORMANCE:
	this routine gets called thousands of times, any tuneups
	will be useful
*/
ws_off_t *
get_vparm(i)
short i;
{
REG int32_t *sp;
REG int32_t v;
	
	sp = MSTACK;
	v = *sp;
	while (i) 
	{
		if (v == STACK_EMARK || v == STACK_MARK) return(0L);
		if (v == STACK_TEMP_VAL) sp += 2;
		else if (v & STACK_VMASK) sp++;
		i--;
		v = *sp;
	}
	if (v & STACK_VARIABLE) 
	{
		return(&SYMT[(short)(VNUM_MASK & v)].value);
	}
	if (v & STACK_FLOATV) 
	{
		return(&FLOATVS[(short)(FLOATVS_MASK & v)]);
	}
	if (v == STACK_TEMP_VAL) return((ws_off_t *)sp + 1);
	return(0L);
}

/*
	PERFORMANCE:
	this routine gets called lots of times, any tuneup will be
	useful 
*/
short
stack_pop(int n)
{
	while (n) 
	{
		if (*MSTACK == STACK_EMARK || *MSTACK == STACK_MARK) return(-1);
		if (*MSTACK == STACK_TEMP_VAL) 
		{
			*MSTACK++ = 0L;
			*MSTACK++ = 0L;
		} else {
			if (*MSTACK & STACK_FLOATV) 
			{
				/* free the floater */
				FLOATVS[(short)*MSTACK & FLOATVS_MASK] = 0;
			}
			if (*MSTACK & STACK_VMASK) *MSTACK++ = 0L;
		}
		n--;
	}
	return(0);
}

/*
	PERFORMANCE:
	this routine gets called lots, tuneup will be useful
*/
void
swapdrop()
{
int32_t l;
short t;
ws_off_t me_off;

	l = MSTACK[0];
	if (l == STACK_TEMP_VAL) me_off = MSTACK[1];
	t = stack_pop(2);
	if (t) return;
	if (l == STACK_TEMP_VAL) 
	{
		*--MSTACK = me_off;
		WS_PTR(me_off)->me_reference = WS_OFF(MSTACK);
	}
	*--MSTACK = l;
}

/*
	upsize the val entry
*/
short
upsizef(te,acc,sca,dacc)
struct ValEnt *te;
int32_t acc;
int32_t sca;
double dacc;
{
struct ValEnt *ne;
union MultiP np,tp;
REG short t,m;
int32_t a,l,tot;

	t = 2;
	if (acc > 32767 || acc < -32768) t = 4;
	if (dacc > 2147483647.0 || dacc < -2147483648.0) t = FLOATSIZE;

	m = 0;
	if (VLEFT && te == (struct ValEnt *)WS_PTR(*VLEFT)) m = 1;
	if (VRIGHT && te == (struct ValEnt *)WS_PTR(*VRIGHT)) m = 2;

	tot = te->ve_length;
	l = compsize(t,te->ve_rank,tot);
	ne = (struct ValEnt *)MakeEnt(l,ME_VAL,(ws_off_t*)0);
	if (!ne) return(0);	/* out of memory */

	switch (m) {		/* te may have moved */
		case 0:
			te = VTEMP ? (struct ValEnt *)WS_PTR(*VTEMP) : te;
			break;
		case 1:
			te = (struct ValEnt *)WS_PTR(*VLEFT);
			break;	
		case 2:
			te = (struct ValEnt *)WS_PTR(*VRIGHT);
			break;	
	}	

	ne->ve_vtype = (te->ve_vtype & ~BYTES_MASK) + t;	/* update type */

	t = te->ve_rank;
	ne->ve_rank = t;
	while (t) {
		t--;
		ne->ve_rho[t] = te->ve_rho[t];
	}
	ne->ve_length = te->ve_length;
	ne->ve_point = (int32_t)((char *)CDATA_AD(ne) - (char *)ne);

	tp.cp = DATA_AD(te);
	np.cp = DATA_AD(ne);
	t = bsizetab[te->ve_vtype & BYTES_MASK];
	m = bsizetab[ne->ve_vtype & BYTES_MASK];
/* skip these if we're scanning backwards */
	if (sca < 0L) {
		sca = ABS(sca);
		tp.cp += sca << t;
		np.cp += sca << m;
		sca = tot - sca;
	}
	while (sca) {
		switch (t) {
			case 0:
				a = (int32_t)(signed char)*tp.cp++;
				break;
			case 1:
				a = (int32_t)*tp.sp++;
				break;
			case 2:
				a = *tp.lp++;
				break;
			default:		/* not a valid source size (never float: this widens integers) */
				a = 0;
				break;
		}
		switch (m) {
			case 1:
				*np.sp++ = (short)a;
				break;
			case 2:
				*np.lp++ = (int32_t)a;
				break;
			case 3:
				*np.dp++ = (double)a; 
				break; 
		}
		sca--;
	}
	if (!VTEMP) {
		MSTACK--;		/* make a place for result on stack */
		VTEMP = (ws_off_t *)MSTACK;
		*--MSTACK = STACK_TEMP_VAL;
	}
	*VTEMP = WS_OFF(ne);
	WS_PTR(*VTEMP)->me_reference = WS_OFF(VTEMP);

	return(1);
}

short
upsize(te,acc,sca)
struct ValEnt *te;
int32_t acc;
int32_t sca;
{
	return upsizef(te,acc,sca,0.0);
}

/*
	proc_quad() -- quad (evaluated) and quote-quad (character) input.
	Quad input reaches the crash log's transcript through
	alt_input_proc(); quote-quad input never passes through that, so it
	is recorded here, as the line the user entered.
*/
void
proc_quad(n)
short n;
{
struct ValEnt *te;
UCHAR *cp,*cpb;
short sl;
UCHAR bf[4];
UCHAR qbuf[LINE_SIZE];		/* local: getin()'s real contract is a LINE_SIZE buffer */

	cp = qbuf;				/* was LSCRATCH, but that's not big enough */

	switch (n) {
		case QUAD_CODE:	/* quad */
			bf[0] = QUAD_CHR;
			bf[1] = ':';
			bf[2] = '\0';
			PRINT(bf);
			NL();
			PRINT("     ");		/* prompt */
			getin(cp);
#ifdef NO_INPUT_PROC
			sl = alt_input_proc(cp,LINEB);
#else
			sl = input_proc(cp,LINEB);
#endif
			// cp = LINEB;
			ERROR_CODE = exec_lineb();	/* this is insecure in a closed application */
			return;

			// break;
		case QQUAD_CODE:
			APL_CASE = 0;
			getin(cp);
			crash_record_input(cp);
			sl = strlen(cp);
			break;
		default:
			ERROR_CODE = SET_ERR(ERR_SYNTAX);
			return;
	}
	te = make_temp(QUOT_CODE,1,(long)sl);
	if (!te) {
		ERROR_CODE = SET_ERR(ERR_WSFULL);
		return;
	}
	cpb = (UCHAR *)DATA_AD(te);
	memcpy(cpb,cp,(long)sl);
	return;
}

void
exec_setup(src,ilb)
UCHAR *src;
short ilb;
{
	IP = src;
	ILEN = *IP++;
	ILEN = (ILEN << 8) + *IP++;
	if (ILEN == -1) 		/* a command? */
	{
		strcpy((char *)LINEA,(char *)IP);
	} else {
		ILEN -= 2;	/* count is included */
		IP += ilb;	/* skip ahead if requested */
		ILEN -= ilb;	/* adjust count */
		/* move to a fixed location */
		if (ILEN) memcpy((char *)LINEA,(char *)IP,ILEN);
	}
	IP = LINEA;
}

void
Adjust_Stack()
{
	/* Init stack */
	if (MSTACK < STACKMAX && (char *)MSTACK > (char *)wspace + wspace->water) 
	{
		while (*MSTACK != STACK_EMARK) *MSTACK++ = 0L;
	}
}

void
end_exec()
{
short r;

/* pop result(s) from MSTACK */

	r = 0;
	if (*MSTACK == STACK_EMARK) return;	/* no results */

	while (*MSTACK != STACK_EMARK) 
	{
		while (*MSTACK == STACK_MARK) 
		{
			*MSTACK++ = 0L;
			r = 0;
		}
		if (*MSTACK == STACK_EMARK) break;

		VLEFT = get_vparm(0);

		if (!VLEFT) 
		{
			ERROR_CODE = SET_ERR(ERR_SYNTAX);	/* most likely error */
			return;
		}

		if (!*VLEFT) 
		{
			ERROR_CODE = SET_ERR(ERR_VALUE);
			return;
		}

		if (LAST_OPERATOR != OP_ASS) 
		{
			vprint(WS_PTR(*VLEFT));
			r = 1;
		} else LAST_OPERATOR = -1;

		stack_pop(1);
	}
	if (r && !DEBUGFLAG) NL();
}


void
print_code(c)
short c;
{
UCHAR *cp,d;
UCHAR t[4];

	cp = op_table;
	while (*cp) 
	{
		if (cp[1] == c) break;
		cp += 2;
	}
	if (!*cp) 
	{
		PRINT("unknown");
		return;
	}
	d = cp[0];
	cp = char_xtable;
	while (*cp) 
	{
		if (cp[2] == d) break;
		cp += 3;
	}
	if (*cp) 
	{
		t[0] = cp[0];
		t[1] = cp[1];
		t[2] = '\0';
	} else {
		t[0] = d;
		t[1] = '\0';
	}
	PRINT(t);
}

void
treport(c,z)
short c,z;
{
char cb[10];
short i;

	if (c & OP_CODE) 
	{
		switch (c & CODE_MASK) 
		{
			case CONST_CODE:	/* constants */
				switch (c) 
				{
					case QUOT_CODE:	/* text */
						PRINT("push quote");
						break;
					case BCONST_CODE:	/* byte */
						PRINT("push byte");
						break;
					case SCONST_CODE:	/* short */
						PRINT("push short");
						break;
					case ICONST_CODE:	/* long */
						PRINT("push long");
						break;
					case FCONST_CODE:	/* float */
						PRINT("push float");
						break;
				}
				break;
			case VAR_CODE:
				i = (short)(c & 0xf) << 8;	/* get sym num */
				i += z;
				if (i >= QQUAD_CODE) PRINT("#/$");
				else {
					if (SYMT[i].type == SYM_FUN) PRINT("exec fn: ");
					else PRINT("push var: ");
					print_name(i);
				}
				break;
			case NFUN_CODE:	/* niladic */
				if (c == SYS_CODE) {	/* masks to NFUN_CODE */
					i = z;
					PRINT("sys func: ");
					sprintf(cb,"%d",i);
					PRINT(cb);
					break;
				}
			case FUN_CODE:	/* monadic or dyadic */
				i = (short)(c & 0xf) << 8;	/* get sym num */
				i += z;
				if (SYMT[i].type == SYM_FUN) {
					PRINT("exec fn: ");
					print_name(i);
				}
				break;
		}
	} else {	/* operators */
		if (c & MONADIC) 
		{
			c &= ~MONADIC;
			if (c == OP_REDUCTION || c == OP_EXP) /* reduction/scan? */
			{
				PRINT("scan/reduction");
			} else {
				PRINT("monop: ");
				print_code(c);
			}
		} else {
			switch (c) 	/* test before get_vparms */
			{
				case OP_NOP:
					PRINT(";");
					break;
				case OP_LB:
					PRINT("[");
					break;
				case OP_IND:
					PRINT("]");
					break;
				case OP_INA:
					PRINT("]=");
					break;
				case OP_OUT:	/* outer product */
					PRINT("o. :");
					sprintf(cb,"%d",c);
					PRINT(cb);
					break;
				default:
					PRINT("dyop: ");
					print_code(c);
					break;
			}
		}
	}
	NL();
}

/*
	stop_push() -- suspension frame for a )STOP. The function stops
	before line FNLINE runs; a resume (->0: pop_fn() then end_fn())
	starts that line from the beginning. Same frame error_push()
	builds, but the position is always 255, end_fn()'s "start of
	line" marker, rather than the bytes left in the line -- that count
	has only 8 bits in the frame, so it can't describe the start of a
	line longer than 254 compiled bytes.
*/
static void
stop_push()
{
	*--MSTACK = (int32_t)(STACK_FNHEAD | FUNC_NUM);
	*--MSTACK = (int32_t)(STACK_FLINE | ((int32_t)FNLINE << FLINE_SHIFT) | 255);
	*--MSTACK = (int32_t)STACK_EMARK;
}

/*
	line_start_trace() -- the TRACEFLAG value for a function line that
	is starting: the line's own )TRACE/)STOP bits, plus a stop on
	every line while )STEP LINE is on and a ->0 has resumed the
	function (step_resumed). Used by do_exec() and
	begin_fn(), the two places a line starts; end_fn() continues a
	line already under way and sets TRACEFLAG itself.
*/
short
line_start_trace(UCHAR flags)
{
short t;

	t = flags & (ME_FTRACE | ME_LTRACE);
	if (step_mode == STEP_LINE && step_resumed) t |= ME_LTRACE;
	return(t);
}

/*
	lexec() -- run the current line. The work is in lexec_run(); this
	wrapper counts nesting, because a stop inside a nested lexec()
	(execute or quad input, via exec_lineb()) can't be resumed
	cleanly -- exec_lineb() carries on as soon as lexec() returns --
	so stops are only taken at depth 1.
*/
static void lexec_run();

void
lexec()
{
	/* a typed line (outer level, no function running) ends any
	   stepping; ->0 on the line sets it again as it resumes */
	if (!lexec_depth && FUNC_NUM == -1) step_resumed = 0;
	lexec_depth++;
	lexec_run();
	lexec_depth--;
}

static void
lexec_run()
{
UCHAR c,r,z;
short i,sfn,sline;
int opstep, opclean;
int32_t *sp_before;
char cb[16];
	
	if (enable_line_events) handle_line_event(1);

	r = z = 0;
	sfn = sline = 0;
	for (;;)
	{
		/* )STOP, and )STEP LINE. TRACEFLAG gets ME_LTRACE only where
		   a function line starts (see line_start_trace()): do_exec()
		   fetching the next line, or begin_fn() starting line 1 in
		   the middle of this loop. end_fn() -- a resume, or a return
		   from a call -- leaves it out, so a resumed line doesn't stop
		   again. Tested before ILEN so an empty line still stops.
		   debug_report() has already shown "FN [n] " for the line;
		   this finishes it as "FN [n] *". Inside execute/quad input
		   the stop is dropped (see lexec()). */
		if (TRACEFLAG & ME_LTRACE)
		{
			TRACEFLAG &= ME_FTRACE;
			if (lexec_depth == 1)
			{
				PRINT("*");
				NL();
				stop_push();
				FNLINE = 0;
				NEXTLINE = 0;
				FUNC_NUM = -1;
				ILEN = 0;
				ERROR_CODE = SET_ERR(ERR_TRACE);
				return;
			}
		}
		if (!ILEN) break;
		if (ILEN == -1)  	/* a command */
		{
			ERROR_CODE = do_command(IP);				
			ILEN = 0;
			break;
		}
		c = *IP++;
		sp_before = MSTACK;	/* stack as it was before this operation */
		opclean = 1;		/* 0 for a user function call, see below */
		/* )STEP OP: note what's about to run, and where, so it can be
		   reported after it runs. Only function code that a ->0
		   resumed steps -- not the typed line (so the ->0 itself
		   doesn't stop), and not a function freshly run from it. */
		opstep = (step_mode == STEP_OP && step_resumed && FUNC_NUM != -1 && lexec_depth == 1);
		if (opstep)
		{
			r = c;
			z = *IP;
			sfn = FUNC_NUM;
			sline = FNLINE;
		}
#ifdef PROFILING
		profile_table[c]++;
#endif

		ILEN--;
		if (c & OP_CODE) 
		{
			switch (c & CODE_MASK) 
			{
				case CONST_CODE:	/* constants */
					switch (c) 
					{
						case QUOT_CODE:	/* text */
							do_quote();
							break;
						case BCONST_CODE:	/* byte */
							do_byte();
							break;
						case SCONST_CODE:	/* short */
							do_short();
							break;
						case ICONST_CODE:	/* long */
							do_long();
							break;
						case FCONST_CODE:	/* float */
							do_float();
							break;
					}
					if (ILEN < 0) ERROR_CODE = SET_ERR(ERR_SYSTEM);
					break;
				case VAR_CODE:	/* VAR */
					i = (short)(c & 0xf) << 8;	/* get sym num */
					i += *IP++;
					ILEN--;
					if (i >= QQUAD_CODE) proc_quad(i);
					else {
						if (SYMT[i].type == SYM_FUN) { opclean = 0; begin_fn(i); }
						else *--MSTACK = STACK_VARIABLE | (int32_t)i;
					}
					break;
				case NFUN_CODE:	/* niladic */
					if (c == SYS_CODE) 	/* masks to NFUN_CODE */
					{
						i = *IP++;
						ILEN--;
						ERROR_CODE = do_sys(i);
						LAST_OPERATOR = -1;
						break;
					}
					/* otherwise, process as FUN_CODE */
				case FUN_CODE:	/* monadic or dyadic */
					i = (short)(c & 0xf) << 8;	/* get sym num */
					i += *IP++;
					ILEN--;
					opclean = 0;
					if (SYMT[i].type == SYM_FUN) begin_fn(i);
					else ERROR_CODE = SET_ERR(ERR_SYNTAX);
					LAST_OPERATOR = -1;	/* acts as op */
					break;
			}
		} else {	/* operators */
			LAST_OPERATOR = c;
			if (c & MONADIC) 
			{
				c &= ~MONADIC;
				/* reduction/scan? */
				if (c == OP_REDUCTION || c == OP_EXP || c == OP_COMPF || c == OP_EXPF)
				{
					if (!ILEN) 
					{
						ERROR_CODE = SET_ERR(ERR_SYNTAX);
						break;
					}
					ILEN--;		/* reserve IP for monop */
				}
				ERROR_CODE = do_monop(c);
			} else {

				switch (c) {	/* test before get_vparms */
					case OP_NOP:
						*--MSTACK = STACK_MARK;
						break;
					case OP_LB:
						ERROR_CODE = do_index();
						break;
					case OP_IND:
						*--MSTACK = INDEX_MARK;
						break;
					case OP_INA:
						*--MSTACK = INDEXA_MARK;
						break;
					case OP_OUT:	/* outer product */
						c = *IP++;	/* gotta be there, was put by parse */
						ILEN--;
						OUTERFLAG = 1;	/* doing outer product */
						ERROR_CODE = do_dyop(c);
						break;
					case OP_INR: {	/* generalized inner product, e.g. +.x */
						UCHAR op1,op2;
						op2 = *IP++;	/* [OP_INR][op2][op1] layout, per parse.c */
						op1 = *IP++;
						ILEN -= 2;
						ERROR_CODE = do_iprod(op1,op2);
						break;
					}
					default:
						OUTERFLAG = 0;	/* not outer product */
						ERROR_CODE = do_dyop(c);
						break;
				}
			}
		}
		if (ERROR_CODE)
		{
			/* The operation failed. Drop anything it pushed before
			   failing (3/0 left an unfilled result on top of its
			   operands), so the stack is as it was just before it:
			   )LEFT/)RIGHT then show its operands. stack_pop() frees
			   float temps and stops at any frame mark (execute's).
			   Not after a user function call: begin_fn() may have
			   pushed saved copies of variables, which must not be
			   dropped here. */
			if (opclean)
				while (MSTACK < sp_before)
					if (stack_pop(1)) break;
			return;
		}
		/* )STEP OP: stop after the operation, reporting it as
		   "FN [n] what". The frame error_push() builds holds the bytes
		   left in the line in 8 bits, with 255 meaning "start of line",
		   so no stop while 255 or more remain; and none after a branch
		   (NEXTLINE moved), where error_push() would record the target
		   line, which may be 0 or past the end -- the next line's
		   first operation stops instead. Resume is ->0, as for any
		   suspension. */
		if (opstep && FUNC_NUM != -1 && NEXTLINE == FNLINE + 1 && ILEN < 255)
		{
			print_name(sfn);
			snprintf(cb,sizeof(cb)," [%d] ",sline);
			PRINT(cb);
			treport((short)r,(short)z);
			error_push();
			FNLINE = 0;
			NEXTLINE = 0;
			FUNC_NUM = -1;
			ILEN = 0;
			ERROR_CODE = SET_ERR(ERR_TRACE);
			return;
		}
	}
}

void
debug_report()
{
char cp[16];	/* " [-32768] " plus NUL needs 11 */

	if (DEBUGFLAG || TRACEFLAG) 
	{
		if (FUNC_NUM != -1 ) 
		{
			NL();
			print_name(FUNC_NUM);
			snprintf(cp,sizeof(cp)," [%d] ",FNLINE);
			PRINT(cp);
		}
	}
}

short
do_exec(src,ilb)
UCHAR *src;
short ilb;
{
short fline,ret;
ws_off_t *f0ep;
struct Func0Ent *f0e;
struct FuncEnt *fe;
char *cp;


	ret = 0;
	cp = (char *)LSCRATCH;

	if (!ilb) Adjust_Stack();		/* a good starting place */

	exec_setup(src,ilb);
	if (ILEN < 1) return(0);	/* nothing to exec */

	do {
		LAST_OPERATOR = -1;
		ERROR_CODE = 0;
		TRACEFLAG = 0;
		if (FUNC_NUM != -1) 
		{
			FNLINE = NEXTLINE;
			NEXTLINE++;
			/* > 0, not just nonzero (V74): a negative target is no line
			   of the function, so it leaves, as 0 and a line past the
			   end do (standard APL). It used to fall into the line walk
			   below, which stops at once for any FNLINE < 1, and run
			   line 1 -- ->¯1 looped forever if line 1 was the branch. */
			if (FNLINE > 0) 
			{
				f0ep = &SYMT[FUNC_NUM].value;
				f0e = (struct Func0Ent *)WS_PTR(*f0ep);
				fe = (struct FuncEnt *)WS_PTR(f0e->f0e_Line1);
				fline = 1;
				while (fe && fline < FNLINE) 	/* kd added "fe &&" 04/24/05, to allow branch to nonexistent line */
				{
					fe = fe->fe_NextLine ? (struct FuncEnt *)WS_PTR(fe->fe_NextLine) : (struct FuncEnt *)0;
					fline++;
				}
				if (fe) 
				{
					exec_setup(fe->fe_exec,0);
					TRACEFLAG = line_start_trace(fe->fe_MemEnt.me_flags);
				} else end_fn();
			} else end_fn();
		}
		debug_report();
		if (!ERROR_CODE) lexec();
		if (ABORTSIG) 
		{
			if (!OUTSYNC) dump_output();
			ABORTSIG = 0;
			ERROR_CODE = SET_ERR(ERR_INTERRUPT);
		}
		if (TRACEFLAG & ME_FTRACE) LAST_OPERATOR = -1;
		if (!ERROR_CODE) end_exec();
		if (ERROR_CODE) 
		{
			if (ERROR_CODE != SET_ERR(ERR_TRACE)) 
			{
				if (ERROR_CODE == SET_ERR(ERR_KILL)) 
				{
					ret = 1;
					goto exec_exit;
				}
				message((short)ERROR_CODE);
				if (FUNC_NUM != -1) 
				{
					if (CURSX) NL();
					print_name(FUNC_NUM);
					sprintf(cp," [%d] *",FNLINE);
					PRINT(cp);
					NL();
					error_push();
					FNLINE = 0;
					NEXTLINE = 0;
					FUNC_NUM = -1;
				}
			}
			ERROR_CODE = 0;
		}
		Adjust_Stack();		/* cleanup */
	} while (FUNC_NUM != -1);

exec_exit:;
	return(ret);
}


/* ==============================================================================
   ==============================================================================
   ============================================================================== */

/*
	new functions for compiling and executing strings on the fly
	used in QUAD-X and QUAD, have not had a lot of debugging...
*/
void
push_valent(struct ValEnt *ve)
{
	*--MSTACK = WS_OFF(ve);
	ve->ve_reference = WS_OFF(MSTACK);
	*--MSTACK = STACK_TEMP_VAL;
}

/* this function used by quad-X & Quad to take the input string, execute so that it can
   be assigned to a result variable.  Actually, the difference between quad-x and quad is only
   the source of the string-- one is the input parameter and the other is from prompt of new kbd
   input.  Most of this cobbled from other contexts, things like interrupts, or nesting have
   had virutally no testing
*/

short
exec_lineb()
{
struct ValEnt *VARHOLD;
ws_off_t *vp;
short err, fline;
char cb[16];
struct Func0Ent *f0e;
struct FuncEnt *fe;

	if (!frame_fits(-1)) return(ERROR_CODE);	/* place can't be saved */
	state_stack(-1,0);

	{ struct FuncEnt *_cf = MakeFNEnt(); wspace->curfunc = _cf ? WS_OFF(_cf) : 0; }
	if (!wspace->curfunc)
	{
		return(ERROR_CODE);
	} else {
		/* non-zero sentinel keeps entry from being GC'd while executing;
		   me_reference is ws_off_t so cannot store &CURFUNC directly --
		   value 1 is safe as long as GarbCollect only tests != 0 */
		CURFUNC->fe_MemEnt.me_reference = (ws_off_t)((char*)&wspace->curfunc - (char*)wspace);
		if (DEBUGFLAG) cdump(CURFUNC->fe_exec);
		exec_setup(CURFUNC->fe_exec,0);
		debug_report();
		if (!ERROR_CODE) lexec();

		/* lexec() runs one line. If the text called a user function,
		   that line was the function's first, and the function is
		   still running (FUNC_NUM != -1): carry on through its lines
		   as do_exec() does -- show each finished line's result, go to
		   the next line, or back to the caller when it ends -- until
		   control is back in the executed text (FUNC_NUM -1 again: the
		   call frame begin_fn() pushed from here is a prompt-style one)
		   and it has finished. do_exec() itself isn't shared, to keep
		   this change away from every other line's path. Stops are
		   skipped at this depth (see lexec()), so no ERR_TRACE. */
		while (!ERROR_CODE && FUNC_NUM != -1)
		{
			if (TRACEFLAG & ME_FTRACE) LAST_OPERATOR = -1;
			end_exec();		/* the finished line's result */
			Adjust_Stack();
			LAST_OPERATOR = -1;
			TRACEFLAG = 0;
			FNLINE = NEXTLINE;
			NEXTLINE++;
			if (FNLINE > 0)		/* negative leaves too -- see do_exec() (V74) */
			{
				f0e = (struct Func0Ent *)WS_PTR(SYMT[FUNC_NUM].value);
				fe = (struct FuncEnt *)WS_PTR(f0e->f0e_Line1);
				fline = 1;
				while (fe && fline < FNLINE)
				{
					fe = fe->fe_NextLine ? (struct FuncEnt *)WS_PTR(fe->fe_NextLine) : (struct FuncEnt *)0;
					fline++;
				}
				if (fe)
				{
					exec_setup(fe->fe_exec,0);
					TRACEFLAG = line_start_trace(fe->fe_MemEnt.me_flags);
				} else end_fn();	/* past the end: back to the caller */
			} else end_fn();		/* ->0 or negative in the function */
			debug_report();
			if (!ERROR_CODE) lexec();
			if (ABORTSIG)
			{
				if (!OUTSYNC) dump_output();
				ABORTSIG = 0;
				ERROR_CODE = SET_ERR(ERR_INTERRUPT);
			}
		}
		wspace->curfunc = 0;
	}

	/* An error in the executed text is the execute's own error (it used
	   to be dropped: this function always returned 0, and whatever was
	   on the stack was taken as the result -- 1 for 1/0). If it happened
	   inside a function the text called, say where, then unwind back to
	   the execute's own frame, restoring each function's localized names
	   (pop_fn()); a suspension here couldn't be resumed. end_fn() below
	   then pops the execute's frame as usual and clears what's left. */
	err = ERROR_CODE;
	VARHOLD = (struct ValEnt *)0;
	if (err)
	{
		if (FUNC_NUM != -1)
		{
			if (CURSX) NL();
			PRINT("In execute: ");
			print_name(FUNC_NUM);
			snprintf(cb,sizeof(cb)," [%d]",FNLINE);
			PRINT(cb);
			NL();
			while (FUNC_NUM != -1)
				if (pop_fn()) break;
		}
	}
	else if (*MSTACK == STACK_TEMP_VAL)
	{
		MSTACK++;
		VARHOLD = (struct ValEnt *)WS_PTR(*MSTACK);
		MSTACK++;
		/* anchor via tmp_ve -- C local &VARHOLD is outside wspace, can't use WS_OFF */
		wspace->tmp_ve = WS_OFF(VARHOLD);
		VARHOLD->ve_reference = WS_OFF(&wspace->tmp_ve);
		debug_check_ref("exec.c/VARHOLD", (struct MemEnt*)VARHOLD, VARHOLD->ve_reference); /* %%% DBG */
	}
	else if (*MSTACK & (STACK_VARIABLE | STACK_FLOATV))
	{
		/* the text ended with a variable (or a float temp), not a
		   temporary array: return a copy of its value, as begin_fn()
		   copies an argument; an undefined name is a Value Error */
		vp = get_vparm(0);
		if (!vp || !*vp) err = SET_ERR(ERR_VALUE);
		else
		{
			VARHOLD = dup_val(vp);
			if (!VARHOLD) err = SET_ERR(ERR_WSFULL);
			else
			{
				wspace->tmp_ve = WS_OFF(VARHOLD);
				VARHOLD->ve_reference = WS_OFF(&wspace->tmp_ve);
				stack_pop(1);
			}
		}
	}
	end_fn();
	if (VARHOLD)
	{
		VARHOLD = (struct ValEnt *)WS_PTR(wspace->tmp_ve);  /* re-derive after end_fn */
		wspace->tmp_ve = 0;
		push_valent(VARHOLD);
	}
	else
	{
		wspace->tmp_ve = 0;
	}

	ERROR_CODE = err;
	return(err);
}

