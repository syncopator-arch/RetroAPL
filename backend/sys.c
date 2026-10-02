/*
	sys.c
*/

#define SYS_C 1

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"

/* File-local macro: cast ws_off_t workspace offset to struct ValEnt* */
#define WSVAL_PTR(off) ((struct ValEnt *)WS_PTR(off))

extern UCHAR *all_ftypes[];
extern PTF all_funcs[];

short
do_sys(f)
short f;
{
static struct ValEnt *re,*le;
UCHAR *types;

	types = all_ftypes[f >> SYSFUNC_SHIFT];
	if (!types) return(SET_ERR(ERR_SYSTEM));

	le = re = (struct ValEnt *)0;

	switch (types[f & SYSFUNC_MASK]) 
	{
		case MONAD:
			VRIGHT = get_vparm(0);
			if (!VRIGHT) return(SET_ERR(ERR_SYNTAX));
			re = WSVAL_PTR(*VRIGHT);
			if (!re) return(SET_ERR(ERR_VALUE));
			break;
		case DYAD:
			VLEFT = get_vparm(0);
			VRIGHT = get_vparm(1);
			if (!VLEFT || !VRIGHT) return(SET_ERR(ERR_SYNTAX));
			le = WSVAL_PTR(*VLEFT);
			re = WSVAL_PTR(*VRIGHT);
			if (!re || !le) return(SET_ERR(ERR_VALUE));
			break;
		case MONAD_OPT_NOASSIGN:	/* MONAD_OPT with no ⎕F←X form
					   (V77); only parse.c treats it
					   differently */
		case MONAD_OPT:
			VRIGHT = get_vparm(0);
			if (VRIGHT) {
				re = WSVAL_PTR(*VRIGHT);
				if (!re) return(SET_ERR(ERR_VALUE));
			} else if (*MSTACK == STACK_MARK) {
				/* parse.c determined at compile time that this
				   occurrence has no argument, and pushed a
				   STACK_MARK immediately ahead of this call so
				   get_vparm() would correctly report "no value"
				   here instead of grabbing a value left pending
				   by some OUTER, still-unresolved operator
				   further right in the source (right-to-left
				   evaluation can leave such a value sitting on
				   MSTACK before we run). Remove that transient
				   mark now -- it's served its purpose, and
				   leaving it would corrupt whatever reads past
				   our own result next (e.g. the outer operator's
				   other operand, or a real index-separator mark
				   further down). */
				MSTACK++;
			}
			break;
	}
	return((*all_funcs[f >> SYSFUNC_SHIFT])(f & SYSFUNC_MASK,le,re));
}

short
push_str(s)
char *s;
{
struct ValEnt *te;
char *cp;
int32_t l;

	l = (int32_t)strlen(s);
	te = make_temp(QUOT_CODE,1,l);
	if (!te) return(SET_ERR(ERR_WSFULL));
	cp = DATA_AD(te);
	memcpy(cp,s,(int)l);
	return(0);
}

short
push_val(c,r,l)
short c;
short r;
int32_t *l;
{
/* NOTE: push_val() has no float path -- every value it returns is
   int32_t, same as get_data()'s int-only side prior to the case
   added above. No quad function currently returns a float result (WOPEN
   returns a handle, WSIZE/WNUMS return dimensions/handles, all naturally
   integral), so this isn't a live bug the way get_data()'s silent-zero
   case was -- just an unhandled case waiting for some future quad
   function that might want one. Flagged here for discoverability rather
   than fixed now, since there's no current caller to fix it against. */
struct ValEnt *te;
union MultiP tp;
short type;
int32_t *lp;
int32_t m,mn,mx;
REG short t;

	lp = l;

	mn = mx = 0;
	t = c;
	while (t--) 
	{
		m = *lp++;
		if (m > mx) mx = m;
		if (m < mn) mn = m;
	}

	type = BCONST_CODE;
	if (mx > 127 || mn < -128) type = SCONST_CODE;
	if (mx > 32767 || mn < -32768) type = ICONST_CODE;
	te = make_temp(type,r,(long)c);
	if (!te) return(SET_ERR(ERR_WSFULL));

	type = type & BYTES_MASK;
	tp.cp = DATA_AD(te);
	while (c--) 
	{
		switch (type) 
		{
			case 1:
				*tp.cp++ = (char)*l++;
				break;
			case 2:
				*tp.sp++ = (short)*l++;
				break;
			case 4:
				*tp.lp++ = *l++;
				break;
		}
	}
	return(0);
}

int32_t
get_data(ve,n)
struct ValEnt *ve;
short n;
{
union MultiP dp;
double dval;

	if (n >= ve->ve_length) return(0);
	dp.cp = DATA_AD(ve);
	switch (ve->ve_vtype & BYTES_MASK) 
	{
		case 1:
			dp.cp += n;
			return((int32_t)(signed char)*dp.cp);
		case 2:
			dp.sp += n;
			return((int32_t)*dp.sp);
		case 4:
			dp.lp += n;
			return((int32_t)*dp.lp);
		case FLOATSIZE:
			/* Previously absent entirely -- a float-typed value
			   fell through every case above and hit the
			   return(0) at the bottom, silently returning zero
			   rather than erroring or even truncating. Worse
			   than a crash: a graphics call with one float
			   coordinate wouldn't fail, it'd just quietly draw
			   at 0 instead of where it was asked to. get_data()
			   is the shared numeric-arg path for every quad
			   function (WOPEN/PEN/POLY/WPOINTS/WCLEAR/WSIZE/
			   FPOLY all go through it), so this is a shared-path
			   fix, not something patched around in grfuncs.c.
			   Round-to-nearest (half away from zero), not
			   truncation-toward-zero (C's default double->int32_t
			   cast) or floor(): truncation biases positive and
			   negative values in opposite directions -- an
			   asymmetry that matters for anything centered
			   around a window's origin, graphics coordinates
			   being the motivating case here -- and floor() biases
			   every negative value one full unit further from
			   zero than intended. Clamped to INTEGER_MAX/
			   INTEGER_MIN first (already defined project-wide for
			   exactly this float-fits-in-int32-check) rather than
			   letting an out-of-range double hit the int32_t cast
			   directly -- that's undefined behavior in C, not
			   just a wraparound. */
			dp.dp += n;
			dval = *dp.dp;
			if (dval > INTEGER_MAX) dval = INTEGER_MAX;
			if (dval < INTEGER_MIN) dval = INTEGER_MIN;
			return((int32_t)(dval >= 0.0 ? dval + 0.5 : dval - 0.5));
	}
	return(0);	/* make compiler happy */
}


double
get_fdata(ve,n)
struct ValEnt *ve;
short n;
{
union MultiP dp;
double dacc;

	dacc = 0.0;
	if (n >= ve->ve_length) return(dacc);
	dp.cp = DATA_AD(ve);
	switch (ve->ve_vtype & BYTES_MASK) 
	{
		case 1:
			dp.cp += n;
			dacc = (double)(signed char)*dp.cp;
			break;
		case 2:
			dp.sp += n;
			dacc = (double)*dp.sp;
			break;
		case 4:
			dp.lp += n;
			dacc = (double)*dp.lp;
			break;
		case FLOATSIZE:
			dp.dp += n;
			dacc = *dp.dp;
			break;	
	}
	return(dacc);
}

