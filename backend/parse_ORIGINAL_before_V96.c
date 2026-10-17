/*
	parse.c
*/

#define PARSE_C 1

#include "parse.h"
#include "mplmem.h"

extern UCHAR *all_ftypes[];
extern void sys_print(unsigned char *s);
extern void NL();

extern UCHAR var_lo,var_hi,alph_lo,alph_hi;
extern UCHAR *all_fnames[];
extern short print_name();
extern short OUT_TRANS_FLAG;	/* platform_translate.c */

/*
	Quad-function syntax mode (V77): QSYN_BOTH, QSYN_WARN or QSYN_NEW,
	see parse.h. Set by the front end (apl_cli's / the GUI's -q option)
	before dll_startup(); a session setting, not saved with the ws.
	qsyn_fn/qsyn_line are set by do_enter() (commands.c) around each
	line it compiles, so a warn note can name the function and line.
	qsyn_fn is -1 everywhere else (typed lines, execute, quad input),
	and no note is printed there.
*/
int quad_syntax = QSYN_BOTH;
short qsyn_fn = -1;
short qsyn_line = 0;

/*
	Tags pass 1 puts in the low nibble of a MONAD_OPT function's
	SYS_CODE byte in LINEC, for pass 2. Nothing else reads LINEC. Pass 2
	strips QTAG_STRICT and QTAG_WARN; QTAG_ASSIGN is SYS_ASSIGN_BIT and
	stays in the compiled byte (see parse.h). The low nibble of SYS_CODE
	was unused before V77, in LINEC and in compiled code.
*/
#define QTAG_ASSIGN SYS_ASSIGN_BIT	/* written ⎕F←X */
#define QTAG_STRICT 0x02		/* -q new, no ←: must take no argument */
#define QTAG_WARN   0x04		/* -q warn, no ←: note it if it takes one */

/*
	qsyn_note() -- under -q warn, during )ENTER: this line uses the old
	form ⎕F X. Prints e.g. "old form ⎕IO X: QAOLD[1]". n is the packed
	module+index byte; the name is the index'th string of the module's
	all_fnames[] list (the same walk input_translate() does).
*/
static void
qsyn_note(UCHAR n)
{
UCHAR *cp;
UCHAR qb[2];
short j;

	if (qsyn_fn < 0) return;
	cp = all_fnames[n >> SYSFUNC_SHIFT];
	for (j = n & SYSFUNC_MASK; j > 0 && *cp; j--) cp += strlen((char *)cp) + 1;
	if (CURSX) NL();
	PRINT((UCHAR *)"old form ");
	/* quad and name as do_help() prints them: internal codes
	   (CHAR_QD, and the name as all_fnames[] holds it, which
	   set_case() flips with op_table) through output translation */
	qb[0] = CHAR_QD;
	qb[1] = '\0';
	OUT_TRANS_FLAG++;
	PRINT(qb);
	PRINT(cp);
	OUT_TRANS_FLAG--;
	PRINT((UCHAR *)" X: ");
	print_name(qsyn_fn);
	sprintf((char *)LINED,"[%d]",qsyn_line);
	PRINT(LINED);
	NL();
}

UCHAR op_table[] = {
	'+',OP_ADD,
	'-',OP_SUB,
	'*',OP_MUL,
	'/',OP_DIV,
	CHAR_AS,OP_ASS,
	'?',OP_QU,
	'!',OP_AB,		/* also modulo */
	CHAR_CE,OP_CE,
	CHAR_FL,OP_FL,
	'i',OP_IOT,
	CHAR_EQ,OP_EQ,
	CHAR_NE,OP_NE,
	CHAR_LE,OP_LE,
	CHAR_GE,OP_GE,
	'p',OP_RHO,
	'<',OP_LT,
	'>',OP_GT,
	']',OP_IND,		/* right bracket (index) */
	'[',OP_LB,
	CHAR_BRA,OP_BRA,
	0xfd,OP_BRA,	/* linux seemed to need this %%% */
	';',OP_NOP,
	'(',OP_LP,
	')',OP_RP,
	',',OP_RAV,
	'&',OP_AND,
	'|',OP_OR,
	'%',OP_COMP,
	'\\',OP_EXP,
	CHAR_COMPF,OP_COMPF,	/* compress, first axis */
	CHAR_EXPF,OP_EXPF,	/* expand, first axis */
	CHAR_CATF,OP_CATF,	/* catenate, first axis */
	CHAR_EPS,OP_EPS,	/* membership, dyadic only */
	CHAR_NAND,OP_NAND,	/* logical NAND */
	CHAR_NOR,OP_NOR,	/* logical NOR */
	CHAR_SQUAD,OP_SQUAD,	/* squad, dyadic index only */
	CHAR_OUT,OP_OUT,
	'.',OP_INR,		/* inner product glue, e.g. +.x -- shares the decimal-point byte;
				   parsenums()'s digit lookahead disambiguates (see parse.h) */
	CHAR_UPG,OP_UPG,
	CHAR_DNG,OP_DNG,
	CHAR_TRA,OP_TRA,
	't',OP_TAK,
	'y',OP_DRP,
	CHAR_HROT,OP_HROT,
	CHAR_VROT,OP_VROT,
	CHAR_QDAS,OP_QDAS,
	CHAR_QTAS,OP_QTAS,
	CHAR_SYS,SYS_CODE,
	CHAR_BAND,OP_BAND,
	CHAR_BOR,OP_BOR,
	'x',OP_EXEC,
	CHAR_LS,OP_LS,
	CHAR_RS,OP_RS,
	'o',OP_OH,
	CHAR_POW,OP_POW,
	CHAR_ENC,OP_ENC,
	CHAR_DEC,OP_DEC,
	CHAR_QX,OP_QX,		/* real Execute -- distinct from 'x'-key OP_EXEC system-command dispatch */
	CHAR_FMT,OP_FMT,	/* real Format */
	CHAR_LOG,OP_LOG,	/* logarithm, monadic and dyadic */
	0
};

/* return symbol number */
short 
lookup_sym(char *vstr)
{
REG short vnum;
short snum;
short scount;
struct NameEnt *ne;

	vnum = 0;	/* could hash this if desired */
	snum = vnum;
	scount = strlen(vstr);
	do {
		if (SYMT[vnum].namelen == scount) 
		{
			if (scount <= SYMNAME_MAX) 
			{
				if (!strncmp(vstr,(char *)SYMT[vnum].name.c,scount)) return(vnum);
			} else {
				ne = (struct NameEnt *)WS_PTR(SYMT[vnum].name.a);
				if (!strncmp(vstr,(char *)ne->ne_data,scount)) return(vnum);
			}
		}
		vnum++;
		if (vnum == SYMTACTUAL) vnum = 0;
	} while (snum != vnum);
	do {
		if (!SYMT[vnum].namelen) {	/* look for empty slot */
			SYMT[vnum].namelen = (UCHAR)scount;
			if (scount <= SYMNAME_MAX) memcpy(SYMT[vnum].name.c,vstr,6);
			else {		/* create new name entry */
				ne = (struct NameEnt *)MakeEnt((long)(scount + (scount & 1) +
					-2 + sizeof(struct NameEnt) - sizeof(struct MemEnt)),
					ME_NAM, &SYMT[vnum].name.a);
				memcpy((char *)ne->ne_data,vstr,(long)scount);
				ne->ne_length = scount;
			}
			SYMT[vnum].type = SYM_UNDEF;
			return(vnum);
		}
		vnum++;
	} while (snum != vnum);
	return(-1);		/* symbol table full */
}

short
enter_sym(UCHAR **out, UCHAR *vstr)
{
short n;
struct Func0Ent *f0e;

	n = lookup_sym(vstr);
	if (n < 0) return(-1);	/* symbol table full */
/*	if (SYMT[n].type == SYM_UNDEF) SYMT[n].type = SYM_VAR; */
	if (SYMT[n].type == SYM_VAR || SYMT[n].type == SYM_UNDEF) {
		*(*out)++ = n & 255;
		*(*out)++ = (n >> 8) | VAR_CODE;
		LASTOP = 0;
	}
	if (SYMT[n].type == SYM_FUN) {
		f0e = (struct Func0Ent *)WS_PTR(SYMT[n].value);
		if (f0e->f0e_right == -1) {		/* treat as var */
			*(*out)++ = n & 255;
			*(*out)++ = (n >> 8) | NFUN_CODE;	
			LASTOP = 0;	/* treat as var */
		} else {	/* treat as op */
			*(*out)++ = n & 255;
			n = (n >> 8) & FUN_MASK;
			if (LASTOP) n |= FUN_CODE;	/* monadic */
			else n |= FUN_CODE;			/* dyadic */
			*(*out)++ = (UCHAR)n;
			LASTOP = 1;
		}
	}
	return(0);
}

UCHAR *get_vstr(UCHAR *cpi)
{
REG short i;

	i = 0;
	while (is_varch(*cpi))
	{
		VBUF[i++] = *cpi++;
		if (i == MAXSYM_CHARS) return(0L);
	}
	if (!i) return(0L);	/* no chars */
	VBUF[i] = 0;
	return(cpi);
}

/*
	parsenums()
*/
short
parsenums(UCHAR **cpip, short *len)
{
REG UCHAR *cpi;
REG short j;
UCHAR c;
short n;
long accum;
double dacc,dacc2;
short digits;
short isfloat,isexp;
short exponent;

	cpi = *cpip;
	c = *cpi;

/* check if numeric */
	isfloat = 0;
	isexp = 0;
	exponent = 0;
	if (ISDIGIT(c) || c == '-' || ((c == '.') && ISDIGIT(cpi[1])) ) {
		*len = 0;	/* rho counter */
		NUM_BASE = 10;		/* base defaults to 10 */
		do {
			n = 0;
			accum = 0L;
			dacc = 0.0;
			digits = 0;
			if (c == '-') 
			{
				c = *(cpi + 1);
				if ((c < '0' || c > '9') && c != '.') break;
				cpi++;	/* advance past - */
				n = 1;	/* the number is negative */
			}
			do {
				c = *cpi;
				if (c >= 'a') c -= 32;
				if (c == 'X' || c == 'x') {
					NUM_BASE = 16;
					cpi++;
					c = *cpi;
				}
				if ((c == 'E' || c == 'e')&& NUM_BASE < 14) {
					c = *++cpi;
					isexp = isfloat = 1;
					switch (c) {
						case '-':
							isexp = -1;
							cpi++;
							break;
						case '+':
							cpi++;
							break;
					}
					c = 0;
					continue;
				}
				
				if (c == '.')
				{
					cpi++;
					digits = 1;
					c = 0;
					if (!isfloat) {	/* convert to floats */
						isfloat = 1;
						for (j=*len-1;j>=0;j--) {
							dacc2 = (double)LSCRATCH[j];
							*((double *)&LSCRATCH[j * 2]) = dacc2;
						}
						*len = *len * 2;
					}
				} else {
					c -= '0';
					if (c > 9) {
						c -= 7;
						if (c < 10) c = 100;	/* illegal symbol */
					}
					if (c < NUM_BASE) {
						cpi++;
						if (isexp) exponent = (exponent * NUM_BASE) + c;
						else {
							accum = (accum * NUM_BASE) + c;
							dacc = (dacc * NUM_BASE) + c;
							if (digits) digits++;
						}
					}
				}
			} while (c < NUM_BASE);
		if (digits > 1) 
			dacc = dacc / pow((double)NUM_BASE,(double)(digits-1));

		if (isexp) {
			exponent = isexp * exponent;	/* sign of exponent */
			dacc = dacc * pow((double)NUM_BASE,(double)exponent);
		}
			if (n) {
				accum = 0 - accum;
				dacc = 0.0 - dacc;
			}
			if (isfloat) {
				*((double *)&LSCRATCH[*len]) = dacc;
				*len += 2;
			} else LSCRATCH[(*len)++] = accum;
			if (*len > 127) return(-4);	/* constant too long */
			c = *cpi;		/* next number must be sep by space */
			if (c != ' ') break;
			while (*cpi == ' ') cpi++;
			c = *cpi;
		} while ((c >= '0' && c <= '9') || c == '.' || c == '-');
		*cpip = cpi;
		return(1+isfloat);
	} else return(0);
}

/* 
	errors:
		-1 max sym chars exceeded
		-2 Sym table overflow
		-3 Syntax error (not char, not num, not in operator table)
		-4 Numeric constant too long
		-5 Missing Left Paren
		-6 Missing Right Paren
		-7 Missing Quote
		-8 Missing Right bracket
*/
short
compline(UCHAR *src, UCHAR *dst)
{
UCHAR *cpi;
REG short i,j;
UCHAR c,*cpo,*cpt;
short n;
int32_t accum,h,l;
struct Func0Ent *f0e;
short op_sp;
short isfloat;

	if (DEBUGFLAG) 
	{
		UCHAR *cp;
		sprintf((char *)LINED,"compline src= ");
		PRINT(LINED);
		cp = src;
		while (*cp) 
		{
			sprintf((char *)LINED,"%02x ",*cp++);
			PRINT(LINED);
		}
		NL();
	}

	cpt = LINEC;
	op_sp = 0;
	cpi = src;	/* source */
	cpo = dst + 2;	/* dest */
	LASTOP = 1;		/* as if op is first */

	while (*cpi) 
	{
		while (*cpi == ' ') cpi++;
		c = *cpi;

/* check if var or fn */
		if ((c >= var_lo && c <= var_hi) || c == ALPH_CHAR)
		{
			cpi = get_vstr(cpi);
			if (!cpi) return(-1);	/* exceeded length */
			while (*cpi == ' ') cpi++;	/* skip spaces */
			if (enter_sym(&cpt,VBUF)) return(-2);
			continue;
		}
	
		/* if last entry not an op, we shouldn't have a number */
		if (!LASTOP) isfloat = 0;
		else {
			isfloat = parsenums(&cpi,&n);
			if (isfloat < 0) return(isfloat);	/* error */
		}

		if (isfloat) 		/* got a number */
		{
			i = n;
			isfloat--;		/* 1=float 0=long */
			if (i) 
			{
				if (!isfloat) 
				{
					h = 0x80000000L;
					l = 0x7fffffffL;
					for (n=0;n<i;n++) 
					{
						accum = LSCRATCH[n];
						if (accum > h) h = accum;
						if (accum < l) l = accum;
					}
					n = ICONST_CODE;
					if (l >= -32768L && h <= 32767L) n = SCONST_CODE;
					if (l >= -128L && h <= 127L) n = BCONST_CODE;
					} else n = FCONST_CODE;
				switch (n) {
					case BCONST_CODE:
						j = 0;
						while (j < i) *cpt++ = LSCRATCH[j++] & 255;
						break;
					case SCONST_CODE:
						j = 0;
						while (j < i) {
							accum = LSCRATCH[j++];
							*cpt++ = (accum >> 8) & 255;
							*cpt++ = accum & 255;
						}
						break;
					case ICONST_CODE:
						j = 0;
						while (j < i) {
							accum = LSCRATCH[j++];
							*cpt++ = (UCHAR)(accum >> 24);
							*cpt++ = (accum >> 16) & 255;
							*cpt++ = (accum >> 8) & 255;
							*cpt++ = accum & 255;
						}
						break;
					case FCONST_CODE:
						j = 0;
						while (j < i) {
							memcpy((char *)cpt,
								(char *)&LSCRATCH[j],FLOATSIZE);
							j += 2;
							cpt += FLOATSIZE;
						}
						break;
				}
				if (isfloat) *cpt++ = i >> 1;
				else
				*cpt++ = (UCHAR)i;		/* length */
				*cpt++ = (UCHAR)n;	/* type */
				LASTOP = 0;
				continue;
			}
		}

/* check if quote */
		if (c == CHAR_QT) 
		{
		short nest;
			cpi++;
			nest = 1;
			n = 0;		/* count chars */
			while (nest) 
			{
				if (!*cpi) return(-7);	/* missing quote */
				if (*cpi == CHAR_QT && *(cpi + 1) == CHAR_QT) 
				{
					cpi += 2;
					*cpt++ = CHAR_QT;
					n++;
				} else {
					if (*cpi == CHAR_QT) 
					{
						nest--;
						cpi++;
					} else {
						*cpt++ = *cpi++;
						n++;
					}
				} 
			}
			*cpt++ = (UCHAR)n;		/* count */
			*cpt++ = QUOT_CODE;
			LASTOP = 0;	/* last entry not an op */
			continue;
		}

/* treat quad and quote quad as special variables */
		if (c == CHAR_QD) 
		{
			*cpt++ = QUAD_CODE & 255;
			*cpt++ = VAR_CODE | (QUAD_CODE >> 8);
			LASTOP = 0;		/* not an op */
			cpi++;
			continue;
		}
		if (c == CHAR_QQ) 
		{
			*cpt++ = QQUAD_CODE & 255;
			*cpt++ = VAR_CODE | (QQUAD_CODE >> 8);
			LASTOP = 0;		/* not an op */
			cpi++;
			continue;
		}

		c = *cpi++;		/* must be an op */
		i = 0;
		while (op_table[i]) 
		{
			if (c == op_table[i++]) 
			{
				switch (op_table[i]) 
				{
					case SYS_CODE:
					{
					UCHAR *pk;
					UCHAR *tagp;	/* this token's SYS_CODE byte */
					short ftype;
					short k;	/* op_table scan, see the peek below */
					short opc;	/* -1 = not an op_table character */

						n = *cpi;
						*cpt++ = *cpi++;
						tagp = cpt;
						*cpt++ = SYS_CODE;
						ftype = all_ftypes[n >> SYSFUNC_SHIFT][n & SYSFUNC_MASK];

						/* Assignment syntax, ⎕IO←0 (V77). For a
						   MONAD_OPT function, a ← after it (past
						   spaces) is consumed here and marks the call
						   as having an argument: everything to the
						   right of the ← is that argument, as for
						   X←.... The ← itself emits nothing; pass 2
						   sees only the tag (QTAG_ASSIGN), which it
						   checks and passes on into the compiled byte
						   as SYS_ASSIGN_BIT. Before V77 a ← here was a
						   Syntax Error at run time, so no working code
						   changes meaning. Not for MONAD_OPT_NOASSIGN
						   (⎕WSIZE, ⎕WNUMS: the ← is left alone and
						   stays a Syntax Error) or any other type.

						   Under -q new (quad_syntax QSYN_NEW), a
						   MONAD_OPT function without ← takes no
						   argument, full stop: LASTOP = 0, so what
						   follows reads as it would after a variable
						   (⎕IO ⍴X is dyadic ⍴, ⎕IO 0 is a Syntax
						   Error), and QTAG_STRICT has pass 2 reject
						   any value it still finds juxtaposed on the
						   right (⎕IO X, ⎕IO (0), ⎕IO 'A' -- pass 1
						   alone can't, see pass 2). One more case is
						   rejected here: a space, then '-' straight
						   before a digit. The negative sign and minus
						   are the same byte here ('-', itrans[0xa2]),
						   so the old form ⎕DIG ¯2 would otherwise
						   silently compile as ⎕DIG-2. ⎕DIG-2 (no
						   space first) and ⎕DIG - 2 stay subtraction.

						   Under -q warn, the old rules below apply
						   unchanged; QTAG_WARN has pass 2 print a
						   note (during )ENTER only) when the call
						   does take an argument. */
						if (ftype == MONAD_OPT) {
							pk = cpi;
							while (*pk == ' ') pk++;
							if (*pk == CHAR_AS) {
								cpi = pk + 1;
								*tagp |= QTAG_ASSIGN;
								LASTOP = 1;
								break;
							}
							if (quad_syntax == QSYN_NEW) {
								if (pk > cpi && *pk == '-' &&
									(ISDIGIT(pk[1]) || (pk[1] == '.' && ISDIGIT(pk[2]))))
									return(-3);
								*tagp |= QTAG_STRICT;
								LASTOP = 0;
								break;
							}
							if (quad_syntax == QSYN_WARN) *tagp |= QTAG_WARN;
						}
						/* from here on, an excluded function is a
						   MONAD_OPT like any other (old rules) */
						if (ftype == MONAD_OPT_NOASSIGN) ftype = MONAD_OPT;
						if (ftype == MONAD_OPT) {
							/* Whether THIS occurrence takes an argument
							   depends on what actually follows it in the
							   source, not on the function's static type --
							   a MONAD_OPT quad func can legally be used
							   either way. Peek past spaces (same idea as
							   the ']=' lookahead below): a genuine
							   value-starting token means it'll consume it
							   (behave like MONAD, LASTOP=1); a definitively
							   dyadic-only operator character, ')', ';', or
							   end of line means it won't (behave like
							   NILAD, LASTOP=0) -- so that operator is
							   correctly seen as dyadic by whatever precedes
							   it, instead of being wrongly forced monadic
							   (see the pass-2 fix for the matching runtime
							   half of this).

							   An operator character that has a real
							   monadic form (a genuine case in monop.c's
							   do_monop(), not just dyop.c) is treated the
							   same as a value-starting token, not lumped
							   in with plain dyadic-only operators: `⎕IO
							   ,1` / `⎕IO ⍴4 7 9` / `⎕WSIZE ⌽1 2` are all
							   genuinely ambiguous grammar -- both "niladic
							   quad, dyadic op" and "op applies monadically,
							   quad consumes the result" are legal parses --
							   and the convention adopted here is to prefer
							   the monadic-consumes-rightward reading,
							   consistent with compline()'s existing
							   default-to-monadic bias elsewhere (e.g.
							   LASTOP starts at 1, pass 2, before any token
							   has been seen). For an operator with NO
							   monadic form at all (e.g. `+`), there's no
							   real ambiguity to begin with -- "niladic
							   quad, dyadic op" is the only legal reading,
							   so it's left in the LASTOP=0 bucket below,
							   same as today.

							   `⎕←`/`⍞←` (OP_QDAS/OP_QTAS) belong in the
							   monadic-capable bucket too: do_monop()'s
							   case for them prints the operand and falls
							   through without popping or replacing it, so
							   the value survives and is legitimately
							   consumable by a MONAD_OPT quad func
							   immediately to its left -- e.g.
							   `Z←⎕IO ⎕←X` (print X for inspection, and
							   still use it to set ⎕IO). Confirmed real
							   usage, not a hypothetical.

							   Still deliberately excluded even though
							   do_monop() has a case for them: OP_EXEC
							   (system-command dispatch) and OP_BRA
							   (branch) -- neither actually leaves a
							   normal consumable value on the stack
							   (OP_EXEC returns a command status directly
							   out of do_monop() instead of pushing a
							   result; OP_BRA is explicitly "branch
							   doesn't output" and just pops), so treating
							   either as value-starting wouldn't produce
							   correct behavior, just trade a clean
							   ERR_SYNTAX for a wrong value or stack
							   imbalance. Also OP_QX (real Execute, ⍎) --
							   recurses into executing another line
							   entirely rather than fitting this
							   leaves-a-plain-value pattern; not confirmed
							   either way, excluded pending an actual case
							   that needs it.

							   And OP_CATF (⍪), left out on purpose although
							   monadic ⍪ (table) exists: its result is always
							   a matrix, which no MONAD_OPT function wants,
							   while the niladic reading ⎕IO⍪X (⎕IO's value
							   joined onto X by dyadic ⍪) is how this always
							   compiled, and is kept (tests/table.apl).

							   The operator check goes through op_table,
							   by OP_ code, not by comparing raw source
							   characters. op_table's letter entries
							   ('i' ⍳, 'p' ⍴, 'o' ○, ...) are case-flipped
							   in place at runtime by the case setting
							   (commands.c), so in the usual mode ⍴
							   arrives as 'P', not 'p'. The earlier
							   version compared against the literal
							   letters, never matched ⍳/⍴/○ in that mode,
							   and silently compiled `⎕IO ⍴4 7 9` as
							   1⍴4 7 9 instead of ⎕IO 3 -- only the
							   punctuation operators, which aren't
							   flipped, got this reading. Looking the
							   character up the same way the tokenizer
							   will a moment later can't disagree with
							   it. The set of OP_ codes below is exactly
							   the old character list, translated. */
							pk = cpi;
							while (*pk == ' ') pk++;
							c = *pk;
							if (c && (is_varch(c) || c == '.' ||
								c == CHAR_QT || c == CHAR_QD ||
								c == CHAR_QQ || c == '(' ||
								c == CHAR_SYS))
								LASTOP = 1;
							else {
								opc = -1;
								if (c) for (k = 0; op_table[k]; k += 2)
									if (op_table[k] == c) {
										opc = op_table[k+1];
										break;
									}
								switch (opc) {
									/* operators with a real monadic
									   form -- see the comment above */
									case OP_SUB:  case OP_MUL:
									case OP_DIV:  case OP_AB:
									case OP_IOT:  case OP_RHO:
									case OP_RAV:  case OP_EXP:
									case OP_QU:   case OP_OH:
									case OP_COMPF: case OP_EXPF:
									case OP_UPG:  case OP_DNG:
									case OP_TRA:  case OP_HROT:
									case OP_VROT: case OP_CE:
									case OP_FL:   case OP_FMT:
									case OP_POW:  case OP_LOG:
									case OP_QDAS: case OP_QTAS:
										LASTOP = 1;
										break;
									default:
										LASTOP = 0;
										break;
								}
							}
						} else LASTOP = ftype ? 1 : 0;
						break;
					}
					case OP_LP:
					case OP_LB:
						*cpt++ = op_table[i];
						LASTOP = 1;		/* treat as operator */
						break;
					case OP_RP:
						*cpt++ = op_table[i];
						LASTOP = 0;	/* not an operator */
						break;
					case OP_NOP:	/* index seperator ; */
					case OP_IND:	/* end of index ] */
						LASTOP = 1;
						if (op_table[i] == OP_IND) {
							while (*cpi == ' ') cpi++;
							if (*cpi == CHAR_AS) {	/* next char an assign */
								cpi++;
								*cpt++ = OP_INA;
							} else {
								*cpt++ = OP_IND;
								LASTOP = 0;
							}
						} else *cpt++ = OP_NOP;
						break;
					case OP_OUT:	/* outer product? always dyadic */
						while (*cpi == ' ') cpi++;
						c = *cpi++;		/* next better be op */
						i = 0;
						while (op_table[i]) {
							if (c == op_table[i++]) {
								*cpt++ = op_table[i];	/* add 2nd op */
								break;
							} else i++;
						}
						if (!op_table[i]) return(-3);	/* no 2nd op */
						*cpt++ = OP_OUT;	/* flag the outer prod */
						LASTOP = 1;
						break;
					case OP_INR:	/* inner product glue: L f.g R, bare '.' */
						if (cpt == LINEC) return(-3);	/* nothing precedes */
						if (*(cpt - 1) & 0xC0) return(-3);	/* not a plain, non-monadic op byte */
						cpt--;
						n = *cpt;		/* op1, popped off cpt */
						while (*cpi == ' ') cpi++;
						c = *cpi++;		/* next better be op2 */
						i = 0;
						while (op_table[i]) {
							if (c == op_table[i++]) {
								*cpt++ = op_table[i];	/* op2 */
								*cpt++ = (UCHAR)n;		/* op1 */
								*cpt++ = OP_INR;		/* flag the inner prod */
								break;
							} else i++;
						}
						if (!op_table[i]) return(-3);	/* no 2nd op */
						LASTOP = 1;
						break;
					default:
						if (LASTOP) *cpt++ = op_table[i] | MONADIC;
						else *cpt++ = op_table[i];
						LASTOP = 1;	/* mark last code was an operator */
						break;
				}
				i = 0;
				break;
			} else i++;
		}
		if (i) return(-3);	/* op not in op table */
	}

/* now fully tokenized, process into output */
	LASTOP = 1;
	while (cpt > LINEC) 
	{
		cpt--;			/* backup to code in process */
		c = *cpt;
		if (c & OP_CODE) 
		{
			switch (c & CODE_MASK) 
			{
				case VAR_CODE:
					*cpo++ = c;
					cpt--;
					*cpo++ = *cpt;
					i = ((c << 8) + *cpt) & VNUM_MASK;
					if (i < QQUAD_CODE) 
					{
						if (!LASTOP && SYMT[i].type != SYM_UNDEF) return(-3);
						if (SYMT[i].type == SYM_UNDEF) LASTOP = 1;
						else LASTOP = 0;
					} else LASTOP = 0;
					goto dypop;
				case NFUN_CODE:
					if ((c & 0xf0) == SYS_CODE) 
					{
						cpt--;
						n = *cpt;
						switch (all_ftypes[n >> SYSFUNC_SHIFT][n & SYSFUNC_MASK]) 
						{
							case 0:		/* var/niladic */
								if (!LASTOP) return(-3);
								*cpo++ = c;
								*cpo++ = (UCHAR)n;
								LASTOP = 0;
								goto dypop;
							case MONAD_OPT_NOASSIGN:	/* never
									   tagged by pass 1, so exactly
									   case 3's old behavior */
							case 3:		/* monadic opt -- decide per
									   occurrence using LASTOP, same
									   invariant CONST_CODE/case 0
									   above rely on: LASTOP==0 means
									   a real value was just resolved
									   immediately to my right (has
									   an argument -- behave like
									   MONAD); LASTOP==1 means
									   nothing real is there (an
									   operator, ')', or start of
									   line), so this call is
									   niladic. Push a STACK_MARK
									   right before the call in that
									   case -- reusing OP_NOP's
									   existing runtime push -- so
									   do_sys()'s get_vparm(0) can't
									   mistake a value some OUTER,
									   still-pending operator already
									   resolved further right on
									   MSTACK for an argument of my
									   own. do_sys()'s MONAD_OPT case
									   (sys.c) pops that mark back
									   off again. */
								/* V77: pass 1's tag (see the
								   QTAG_ defines) is in c's low
								   nibble. Keep only SYS_ASSIGN_BIT
								   for the compiled byte. An
								   ← with nothing resolved to its
								   right (⎕IO←, (⎕IO←)) is a
								   Syntax Error; so, under -q new,
								   is a value juxtaposed without
								   one (⎕IO X). Under -q warn,
								   taking an argument without ←
								   gets a note (during )ENTER). */
								{
								UCHAR tag = c & 0x0f;

									c = SYS_CODE | (tag & SYS_ASSIGN_BIT);
									if (LASTOP) {
										if (tag & QTAG_ASSIGN) return(-3);
										*cpo++ = OP_NOP;
										*cpo++ = c;
										*cpo++ = (UCHAR)n;
										LASTOP = 0;
										goto dypop;
									}
									if (tag & QTAG_STRICT) return(-3);
									if (tag & QTAG_WARN) qsyn_note((UCHAR)n);
								}
								/* falls through: a real value is
								   available -- treat like MONAD */
							case 1:		/* monadic */
								*cpo++ = c;
								*cpo++ = (UCHAR)n;
								/* a real value was just resolved
								   immediately to the right -- same
								   invariant CONST_CODE/VAR_CODE rely
								   on. Reaches here either as a plain
								   MONAD quad func, or via case 3's
								   fallthrough for a MONAD_OPT quad
								   func that turned out to have an
								   argument -- both cases resolve a
								   value right here, so both need
								   LASTOP=0, not 1 (confirmed failing
								   before this fix: `⎕IO ⎕UCASE 'a'`,
								   `⎕IO ⎕WSIZE 1`). */
								LASTOP = 0;
								break;
							case 2:		/* dyadic */
								OP_STACK[op_sp++] = *cpt;
								OP_STACK[op_sp++] = c;
								LASTOP = 1;
								break;
						}
						break;
					} else {
						if (!LASTOP) return(-3);
						LASTOP = 0;
					}
					*cpo++ = c;
					cpt--;
					*cpo++ = *cpt;
dypop:;
					if (op_sp) {
						c = OP_STACK[op_sp - 1];
						if (c != OP_RP && c != OP_IND && c != OP_INA) {
							op_sp--;
							*cpo++ = c;
							if (c == OP_OUT) *cpo++ = OP_STACK[--op_sp];
							if (c == OP_INR) {
								*cpo++ = OP_STACK[--op_sp];	/* op2 */
								*cpo++ = OP_STACK[--op_sp];	/* op1 */
							}
							if (c == OP_LB) {
								if (!op_sp) return(-8);
								c = OP_STACK[--op_sp];
								if (c == OP_IND) goto dypop;
								if (c != OP_INA) return(-8);
							}
							if (c & OP_CODE) *cpo++ = OP_STACK[--op_sp];
						}
					}
					break;
				case FUN_CODE:
					LASTOP = 1;		/* acts like operator -- correct
							   for the deferred (dyadic) branch
							   below, where nothing's resolved
							   yet; the monadic branch overrides
							   this once it actually resolves a
							   value */
					cpt--;
					n = (c & FUN_MASK) << 8;
					n += *cpt;
					f0e = (struct Func0Ent *)WS_PTR(SYMT[n].value);
					if (f0e->f0e_left == -1) {	  /* monadic ? */
						*cpo++ = c;
						*cpo++ = *cpt;
						/* a real value was just resolved
						   immediately to the right -- same
						   invariant CONST_CODE/VAR_CODE rely
						   on (confirmed failing before this
						   fix: `⎕IO DOUBLE 1`) */
						LASTOP = 0;
					} else {		/* stack dyadics */
						OP_STACK[op_sp++] = *cpt;
						OP_STACK[op_sp++] = c;
					}
					break;
				case CONST_CODE:
					if (!LASTOP) return(-3);
					LASTOP = 0;
					cpt--;
					n = *cpt;	/* count */
					i = n * (c & BYTES_MASK);	/* total bytes */
					*cpo++ = c;	/* code */
					*cpo++ = (UCHAR)n;	/* count */
					cpt -= i;	/* backup to beginning */
					memcpy(cpo,cpt,i);	/* move into place */
					cpo += i;
					goto dypop;
			}
		} else {		/* an operator */
			LASTOP = 1;
			switch (c) {
				case OP_IND:		/* ], used as marker */
				case OP_INA:		/* ]=, used as marker */
					*cpo++ = c;
					OP_STACK[op_sp++] = c;	/* mark stack too */
					break;
				case OP_NOP:		/* ;, used as marker */
					*cpo++ = c;
					break;
				case OP_LB:			/* [ is always dyadic */
				case OP_RP:
					OP_STACK[op_sp++] = c;
					break;
				case OP_LP:
					if (!op_sp) return(-6);
					if (OP_STACK[--op_sp] != OP_RP) return(-6);
					LASTOP = 0;
					goto dypop;
				default:
					if (c & MONADIC) {
						*cpo++ = c;
						/* a real value was just resolved
						   immediately to the right -- same
						   invariant CONST_CODE/VAR_CODE rely
						   on (this is the original bug:
						   `⎕IO ,1`, `⎕IO ⍴4 7 9`,
						   `⎕WSIZE ⌽1 2`) */
						LASTOP = 0;
					}
					else {
						if (c == OP_OUT) {
							c = *--cpt;
							OP_STACK[op_sp++] = c;
							OP_STACK[op_sp++] = OP_OUT;
						} else if (c == OP_INR) {
							c = *--cpt;		/* op1 */
							OP_STACK[op_sp++] = c;
							c = *--cpt;		/* op2 */
							OP_STACK[op_sp++] = c;
							OP_STACK[op_sp++] = OP_INR;	/* marker on top */
						} else OP_STACK[op_sp++] = c;
					}
					break;
			}
		}
	}

	if (op_sp) return(-3);

	i = cpo - dst;
/*	i -= 2;   length includes count */
	dst[0] = (i >> 8);
	dst[1] = i & 255;
	return(0);
}

