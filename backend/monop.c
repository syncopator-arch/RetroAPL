/*
	monop.c
*/

#define MONOP_C 1

#include <stdlib.h>	/* malloc/free for scanredf's scan accumulator array */
#include <math.h>	/* exp()/isfinite() for monadic * */
#include "parse.h"
#include "mplmem.h"
#include "externs.h"

/* Narrow decls for what fmt_number()/prep_exec_line() need from funcs.h.
   Not #including funcs.h itself: its unguarded `extern short do_dhrot();`
   (old-style empty parens) conflicts with this file's own real prototype
   `short do_dhrot(short f);` above under modern C's stricter empty-parens
   rules -- externs.h avoids this collision with an #ifndef MONOP_C guard,
   funcs.h doesn't have one. */
extern int32_t get_data();
extern double get_fdata();
extern short  floatlen(double f, short w, int *dpp, int *efp);
extern short push_str();
extern int step_resumed;	/* exec.c -- )STEP */
extern short resume_line(int32_t n);	/* fn.c -- ->N at the prompt */

#ifndef PI
#define PI 3.14159265358979323846	/* full double precision; was 3.1415928, wrong in the 7th place */
#endif

/*
 * File-local macro: cast a ws_off_t workspace offset to struct ValEnt*.
 * WS_PTR() always returns struct MemEnt*; use WSVAL_PTR() wherever
 * ValEnt fields (ve_rank, ve_rho, ve_vtype, ve_length, ve_point) are needed.
 */
#define WSVAL_PTR(off) ((struct ValEnt *)((char *)wspace + (off)))

/*
 * Local prototypes — override the empty-parameter-list extern declarations
 * in externs.h so the compiler sees the real signatures before the definitions.
 */
static void           supdate(struct ValEnt *te);
static void           shuffle(unsigned char *pp, int32_t be, int32_t bp);
short reduce_identity(short op, UCHAR dtype, int32_t *acc, double *dacc);
short reduce_combine(short op, short sdir, UCHAR dtype, int32_t *acc, int32_t acc2, double *dacc, double dacc2);
#define REDUCE_IDENTITY(op, dtype) reduce_identity((op), (dtype), &acc, &dacc)
#define REDUCE_COMBINE(op, sdir, dtype) reduce_combine((op), (sdir), (dtype), &acc, acc2, &dacc, dacc2)
short do_dhrot(short f);
short do_dvrot(short f);
short do_ohrot(void);
short do_ovrot(void);
short do_monop(UCHAR c);
short fmt_number(struct ValEnt *re, char *cp);
short prep_exec_line(struct ValEnt *re);

/* supdate -- after a horizontal shuffle of a struct array, walk the
 * elements and fix their ve_reference fields so each one points back to
 * the int32_t slot that holds its ws_off_t offset.
 * tp.lp now iterates over int32_t (ws_off_t) slots, not raw pointers.
 */
static void
supdate(struct ValEnt *te)
{
	struct ValEnt *re;
	int32_t       l;
	union MultiP  tp;

	l      = te->ve_length;
	tp.cp  = DATA_AD(te);

	while (l--) {
		if (*tp.lp) {
			re = WSVAL_PTR(*tp.lp);
			re->ve_reference = WS_OFF(tp.lp);
			debug_check_ref("monop.c/tp.lp", (struct MemEnt*)re, re->ve_reference); /* %%% DBG */
		}
		tp.lp++;
	}
}

/* reduce_identity -- seed acc and dacc  with the identity
 * element of operator `op`, for a zero-length reduction/scan axis where
 * acc/dacc would otherwise be left uninitialized. Shared by both the
 * last-axis (scanred) and first-axis (scanredf) fold loops -- same
 * identity element regardless of which axis is being walked.
 * Returns 0 on success, or a SET_ERR() code if `op` has no identity
 * (caller should return this value directly). */
short
reduce_identity(short op, UCHAR dtype, int32_t *acc, double *dacc)
{
	if (dtype == FLOATSIZE) {
		switch (op) {
			case OP_MUL: *dacc = 1.0;       break;
			case OP_ADD: *dacc = 0.0;       break;
			case OP_SUB: *dacc = 0.0;       break;
			case OP_CE:  *dacc = -HUGE_VAL; break;
			case OP_FL:  *dacc =  HUGE_VAL; break;
			case OP_AND: *dacc = 1.0;       break;
			case OP_OR:  *dacc = 0.0;       break;
			case OP_EQ:  *dacc = 1.0;       break;
			case OP_NE:  *dacc = 0.0;       break;
			case OP_LE:  *dacc = 1.0;       break;
			case OP_GE:  *dacc = 1.0;       break;
			case OP_GT:  *dacc = 0.0;       break;
			case OP_LT:  *dacc = 0.0;       break;
			default: return(SET_ERR(ERR_SYNTAX));
		}
		return(0);
	}
	switch (op) {
		case OP_MUL: *acc = 1L;        break;
		case OP_ADD: *acc = 0L;        break;
		case OP_SUB: *acc = 0L;        break;
		case OP_CE:  *acc = INT32_MIN; break;
		case OP_FL:  *acc = INT32_MAX; break;
		case OP_AND: *acc = 1L;        break;
		case OP_OR:  *acc = 0L;        break;
		case OP_EQ:  *acc = 1L;        break;
		case OP_NE:  *acc = 0L;        break;
		case OP_LE:  *acc = 1L;        break;
		case OP_GE:  *acc = 1L;        break;
		case OP_GT:  *acc = 0L;        break;
		case OP_LT:  *acc = 0L;        break;
		default: return(SET_ERR(ERR_SYNTAX));
	}
	/* Shadow dacc with the same identity, in double, so it stays a
	   faithful running total even for int-typed reductions -- needed
	   so case 4's overflow check below has something trustworthy to
	   test once *acc itself may have already wrapped. */
	*dacc = (double)*acc;
	return(0);
}

/* reduce_combine -- fold one more element (acc2/dacc2) into the running
 * accumulator (*acc and *dacc) for operator `op`, in direction `sdir`
 * (-1 = reduce/reverse, +1 = scan/forward; only OP_SUB cares).  Shared
 * by scanred and scanredf for the same reason as reduce_identity above.
 * Returns 0 on success, or a SET_ERR() code for an unsupported op. */
short
reduce_combine(short op, short sdir, UCHAR dtype, int32_t *acc, int32_t acc2, double *dacc, double dacc2)
{
	if (dtype == FLOATSIZE) {
		switch (op) {
			case OP_MUL: *dacc *= dacc2; break;
			case OP_ADD: *dacc += dacc2; break;
			case OP_SUB:
				if (sdir < 0) *dacc = dacc2 - *dacc;
				else          *dacc -= dacc2;
				break;
			case OP_CE:  if (*dacc < dacc2) *dacc = dacc2; break;
			case OP_FL:  if (*dacc > dacc2) *dacc = dacc2; break;
			case OP_AND: *dacc = (*dacc && dacc2) ? 1.0 : 0.0; break;
			case OP_OR:  *dacc = (*dacc || dacc2) ? 1.0 : 0.0; break;
			case OP_EQ:  *dacc = (*dacc == dacc2) ? 1.0 : 0.0; break;
			case OP_NE:  *dacc = (*dacc != dacc2) ? 1.0 : 0.0; break;
			case OP_LE:  *dacc = (*dacc >= dacc2) ? 1.0 : 0.0; break;
			case OP_GE:  *dacc = (*dacc <= dacc2) ? 1.0 : 0.0; break;
			case OP_GT:  *dacc = (*dacc < dacc2) ? 1.0 : 0.0; break;
			case OP_LT:  *dacc = (*dacc > dacc2) ? 1.0 : 0.0; break;
			default: return(SET_ERR(ERR_SYNTAX));
		}
		return(0);
	}
	/* + - x computed in 64 bits and narrowed back (V75): the int32
	   arithmetic overflowed -- undefined in C, reported by UBSan --
	   once a fold passed int32 (x/10000+iota10). The narrowed *acc
	   still wraps; nothing trusts it then -- every write-out checks
	   the shadow *dacc first and promotes to float. */
	switch (op) {
		case OP_MUL: *acc = (int32_t)((int64_t)*acc * acc2); break;
		case OP_ADD: *acc = (int32_t)((int64_t)*acc + acc2); break;
		case OP_SUB:
			if (sdir < 0) *acc = (int32_t)((int64_t)acc2 - *acc);
			else          *acc = (int32_t)((int64_t)*acc - acc2);
			break;
		case OP_CE:  if (*acc < acc2) *acc = acc2; break;
		case OP_FL:  if (*acc > acc2) *acc = acc2; break;
		case OP_AND: *acc = (*acc && acc2) ? 1L : 0L; break;
		case OP_OR:  *acc = (*acc || acc2) ? 1L : 0L; break;
		case OP_EQ:  *acc = (*acc == acc2) ? 1L : 0L; break;
		case OP_NE:  *acc = (*acc != acc2) ? 1L : 0L; break;
		case OP_LE:  *acc = (*acc >= acc2) ? 1L : 0L; break;
		case OP_GE:  *acc = (*acc <= acc2) ? 1L : 0L; break;
		case OP_GT:  *acc = (*acc < acc2) ? 1L : 0L; break;
		case OP_LT:  *acc = (*acc > acc2) ? 1L : 0L; break;
		default: return(SET_ERR(ERR_SYNTAX));
	}
	/* Shadow the same fold in double, using dacc2 (the caller now
	   populates it for every dtype, not just FLOATSIZE -- see scantop/
	   scantopf). *acc may already have wrapped by the time case 4's
	   write-out gets to check it; *dacc, computed independently here
	   rather than derived from *acc, hasn't. */
	switch (op) {
		case OP_MUL: *dacc *= dacc2; break;
		case OP_ADD: *dacc += dacc2; break;
		case OP_SUB:
			if (sdir < 0) *dacc = dacc2 - *dacc;
			else          *dacc -= dacc2;
			break;
		case OP_CE:  if (*dacc < dacc2) *dacc = dacc2; break;
		case OP_FL:  if (*dacc > dacc2) *dacc = dacc2; break;
		case OP_AND: *dacc = (*dacc && dacc2) ? 1.0 : 0.0; break;
		case OP_OR:  *dacc = (*dacc || dacc2) ? 1.0 : 0.0; break;
		case OP_EQ:  *dacc = (*dacc == dacc2) ? 1.0 : 0.0; break;
		case OP_NE:  *dacc = (*dacc != dacc2) ? 1.0 : 0.0; break;
		case OP_LE:  *dacc = (*dacc >= dacc2) ? 1.0 : 0.0; break;
		case OP_GE:  *dacc = (*dacc <= dacc2) ? 1.0 : 0.0; break;
		case OP_GT:  *dacc = (*dacc < dacc2) ? 1.0 : 0.0; break;
		case OP_LT:  *dacc = (*dacc > dacc2) ? 1.0 : 0.0; break;
		default: return(SET_ERR(ERR_SYNTAX));
	}
	return(0);
}

static void
shuffle(unsigned char *pp, int32_t be, int32_t bp)
{
	int32_t        m, t;
	int32_t        bs;
	unsigned char  h;
	int32_t        l;
	unsigned char *p1, *p2;

	bs = 0;
	bp = be - bp;

	m = be - bp;
	t = bp - bs;

	while (m > t ? t : m) {
		if (m > t) {
			p1 = pp + bs;
			p2 = pp + (be - t);
			l  = t;
			while (l--) {
				h    = *p1;
				*p1++ = *p2;
				*p2++ = h;
			}
			be -= t;
		} else {
			p1 = pp + bs;
			p2 = pp + bp;
			l  = m;
			while (l--) {
				h    = *p1;
				*p1++ = *p2;
				*p2++ = h;
			}
			bs += m;
		}
		m = be - bp;
		t = bp - bs;
	}
}

short
do_dhrot(short f)
{
	short          i, j;
	struct ValEnt *re, *le = 0L;
	union MultiP   rp, lp;
	int32_t        l, m, t, tc, rc = 0, v;
	double         d2;

	re = WSVAL_PTR(*VRIGHT);
	l  = re->ve_length;
	j  = re->ve_rank;

	if (!(re->ve_vtype & VARIABLE_BIT)) {
		re = dup_val(VRIGHT);
		if (!re) return(SET_ERR(ERR_WSFULL));
	}

	m = 1L;
	if (j) m = re->ve_rho[0];

	lp.cp = 0L;	/* le, lp, rc: set and used only when f (dyadic) */
	if (f) {
		if (!*VLEFT) return(SET_ERR(ERR_VALUE));
		le = WSVAL_PTR(*VLEFT);
		lp.cp = DATA_AD(le);
		rc    = 1L;
		if (le->ve_rank) rc = le->ve_rho[0];
		v = 0L;
	}

	l     = l / m;
	rp.cp = DATA_AD(re);
	i     = re->ve_vtype & BYTES_MASK;

	while (l--) {		/* for every row in the array */
		tc = m;
		if (f) {		/* dyadic rotate */
			switch (le->ve_vtype & BYTES_MASK) {
				case 1:
					t = m - (signed char)*(lp.cp + v);
					break;
				case 2:
					t = m - *(lp.sp + v);
					break;
				case 4:
					t = m - *(lp.lp + v);
					break;
				case FLOATSIZE:
					t = (int32_t)(m - *(lp.dp + v));
					break;
				default:		/* not a valid element size */
					t = 0;
					break;
			}
			while (t >= m) t -= m;
			while (t < 0L) t += m;
			v++;
			if (v >= rc) v = 0L;
			switch (i) {
				case 2:
					tc = tc << 1;
					t  = t  << 1;
					break;
				case 4:
					tc = tc << 2;
					t  = t  << 2;
					break;
				case FLOATSIZE:
					tc = tc << FLOATSHIFT;
					t  = t  << FLOATSHIFT;
					break;
			}
			shuffle((unsigned char *)rp.cp, tc, t);

		} else {		/* monadic rotate */
			t = m >> 1;	/* t = row width / 2 */
			while (t--) {
				switch (i) {
					case 1:
						v             = rp.cp[t];
						rp.cp[t]      = rp.cp[m - t - 1];
						rp.cp[m-t-1] = (char)v;
						break;
					case 2:
						v             = rp.sp[t];
						rp.sp[t]      = rp.sp[m - t - 1];
						rp.sp[m-t-1] = (short)v;
						tc            = m << 1;
						break;
					case 4:
						v             = rp.lp[t];
						rp.lp[t]      = rp.lp[m - t - 1];
						rp.lp[m-t-1] = v;
						tc            = m << 2;
						break;
					case FLOATSIZE:
						d2            = rp.dp[t];
						rp.dp[t]      = rp.dp[m - t - 1];
						rp.dp[m-t-1] = d2;
						tc            = m << FLOATSHIFT;
						break;
				}
			}
		}
		rp.cp += tc;
	}

	/* pop everything */
	if (f) stack_pop(2);
	else   stack_pop(1);

	/* if struct, update the shuffled pointers */
	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) supdate(re);

	temp_stack(re);		/* return result */
	return(0);
}

/* do_ohrot -- outer product horizontal rotate (L ∘.⌽ R).
 *
 * do_dhrot() rotates R's own buffer in place, cycling one left element
 * per row of R -- correct for conformant dyadic ⌽ but structurally unable
 * to produce an outer-product result (no combined L++R shape, no lscan*rscan
 * output). This is the outer-product counterpart: for each of the lscan
 * scalars in L, take a fresh copy of the whole of R and rotate every row of
 * that copy uniformly by that one amount (the same as ordinary dyadic ⌽
 * with a scalar left argument), stacking the lscan copies into a result of
 * shape (⍴L),(⍴R). Never writes into R's own storage -- only reads it as
 * a template to copy from -- so R itself is left untouched regardless of
 * whether it is a variable or a temp.
 */
short
do_ohrot(void)
{
	short          i, b;
	struct ValEnt *re, *le, *te;
	union MultiP   lp, srcp, dstp;
	int32_t        m, rows, rowcnt, k, rscan, lscan, elemsize, tc, t, amt;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	rscan = re->ve_length;
	lscan = le->ve_length;

	m = 1L;
	if (re->ve_rank) m = re->ve_rho[0];	/* row width (fastest axis) */
	rows = m ? rscan / m : 0L;		/* rows per copy of R */

	elemsize = re->ve_vtype & BYTES_MASK;

	b  = re->ve_rank + le->ve_rank;
	VTEMP = (ws_off_t*)0;
	te = make_temp((short)re->ve_vtype, b, rscan * lscan);
	if (!te) return(SET_ERR(ERR_WSFULL));

	/* these may have moved during allocation */
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	/* result shape = (⍴L),(⍴R) -- same fill order used by the generic
	   outer-product path in do_dyop() */
	i = le->ve_rank;
	while (i) te->ve_rho[--b] = le->ve_rho[--i];
	i = re->ve_rank;
	while (i) te->ve_rho[--b] = re->ve_rho[--i];

	lp.cp   = DATA_AD(le);
	dstp.cp = DATA_AD(te);

	for (k = 0; k < lscan; k++) {
		switch (le->ve_vtype & BYTES_MASK) {
			case 1:
				amt = (int32_t)(signed char)*lp.cp;
				lp.cp++;
				break;
			case 2:
				amt = (int32_t)*lp.sp;
				lp.sp++;
				break;
			case 4:
				amt = (int32_t)*lp.lp;
				lp.lp++;
				break;
			case FLOATSIZE:
			{
				/* V74: reduce by the row width before converting --
				   rotating by any amount is meaningful (1E10), and
				   converting an out-of-range double is undefined.
				   Only NaN/infinity is left to fail. */
				double d = *lp.dp++;
				d = (m > 0) ? fmod(d, (double)m) : 0.0;
				if (!dbl_to_i32(d, &amt)) return(SET_ERR(ERR_DOMAIN));
				break;
			}
			default:		/* not a valid element size */
				amt = 0;
				break;
		}

		/* fresh copy of the whole right array for this left element */
		memcpy(dstp.cp, DATA_AD(re), (size_t)(rscan * elemsize));

		/* V74: reduce amt first, so the loops below run at most
		   once each (a huge integer amount used to loop millions
		   of times) and m - amt can't overflow; and guard m == 0,
		   an empty last axis, where "while (t >= m) t -= m" never
		   ended. */
		if (m > 0) {
			amt %= m;
			t = m - amt;
			while (t >= m) t -= m;
			while (t <  0) t += m;
		} else t = 0;
		tc = m;
		switch (elemsize) {
			case 2: tc <<= 1; t <<= 1; break;
			case 4: tc <<= 2; t <<= 2; break;
			case FLOATSIZE: tc <<= FLOATSHIFT; t <<= FLOATSHIFT; break;
		}

		/* rotate every row of this copy uniformly by t (same amount) */
		srcp.cp = dstp.cp;
		rowcnt  = rows;
		while (rowcnt--) {
			shuffle((unsigned char *)srcp.cp, tc, t);
			srcp.cp += tc;
		}

		dstp.cp += rscan * elemsize;
	}

	/* pop everything -- same convention used by do_diota() and the
	   generic engine in do_dyop(): both reset VTEMP just before calling
	   make_temp() and check it here afterward, so it looks like
	   make_temp()/allocation can itself leave a third slot on the
	   machine stack that needs popping. Following the same pattern
	   defensively rather than assuming stack_pop(2) is always right. */
	if (VTEMP) stack_pop(3);
	else stack_pop(2);

	/* if struct, update the shuffled reference slots, same as do_dhrot */
	if ((te->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) supdate(te);

	temp_stack(te);		/* return result */
	return(0);
}

short
do_dvrot(short f)
{
	short          i, j;
	struct ValEnt *re, *le = 0L, *te;
	union MultiP   rp, lp, tp;
	int32_t        l, m, t, tm, tb, tc = 0, tl, rc = 0;

	re = WSVAL_PTR(*VRIGHT);

	/* if struct, disallow this move, as it's a big pain to implement */
	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE)
		return(SET_ERR(ERR_DOMAIN));

	l  = re->ve_length;
	j  = re->ve_rank;
	te = dup_val(VRIGHT);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);

	m = 1L;
	for (i = 0; i < (j - 1); i++) m *= te->ve_rho[i];

	lp.cp = 0L;	/* le, lp, rc: set and used only when f (dyadic) */
	if (f) {
		if (!*VLEFT) return(SET_ERR(ERR_VALUE));
		le = WSVAL_PTR(*VLEFT);
		lp.cp = DATA_AD(le);
		rc    = 1L;
		if (le->ve_rank) rc = le->ve_rho[0];
	} else {
		tc = l - m;
		tb = 0L;
	}
	tl    = l - 1;
	tm    = 0L;
	rp.cp = DATA_AD(re);
	tp.cp = DATA_AD(te);
	i     = re->ve_vtype & BYTES_MASK;

	while (l--) {
		if (f) {
			switch (le->ve_vtype & BYTES_MASK) {
				case 1:
					t = m * (signed char)*(lp.cp + tm);
					break;
				case 2:
					t = m * *(lp.sp + tm);
					break;
				case 4:
					t = m * *(lp.lp + tm);
					break;
				case FLOATSIZE:
					t = (int32_t)(m * *(lp.dp + tm));
					break;
				default:		/* not a valid element size */
					t = 0;
					break;
			}
			t = (0L - t) + (tl - l);
			tm++;
			if (tm == rc) tm = 0L;
			while (t > tl)  t -= tl + 1;
			while (t < 0L)  t += tl + 1;
		} else {
			t = (tc + tm++) - tb;
			if (tm == m) {
				tm  = 0L;
				tb += m;
			}
		}
		switch (i) {
			case 1:
				*(tp.cp + t) = *rp.cp++;
				break;
			case 2:
				*(tp.sp + t) = *rp.sp++;
				break;
			case 4:
				*(tp.lp + t) = *rp.lp++;
				break;
			case FLOATSIZE:
				*(tp.dp + t) = *rp.dp++;
				break;
		}
	}

	/* pop everything */
	if (f) stack_pop(2);
	else   stack_pop(1);

	temp_stack(te);		/* return result */
	return(0);
}

/* do_ovrot -- outer product vertical rotate (L ∘.⊖ R).
 *
 * Same overall strategy as do_ohrot(): do_dvrot() scatters R's own
 * elements into a same-shaped duplicate using a per-element target index
 * t, with the left argument optionally cycling one amount per "row" via
 * rc/tm for conformant dyadic ⊖. There's no path in it to a combined
 * L++R shape or an lscan*rscan output.
 *
 * For outer product, each of the lscan left scalars gets its own full
 * pass over R using that single amount uniformly for every element (the
 * rc==1 case of do_dvrot's scatter math, generalized: with a single
 * amount per pass, the left-cycling term (tl - l) collapses to a plain
 * ascending element counter, so it's reproduced directly here as idx
 * rather than by re-deriving do_dvrot's decrementing-l trick). The lscan
 * results are stacked into a result of shape (⍴L),(⍴R). R itself is only
 * ever read, never written, regardless of variable/temp status.
 *
 * Structs are not supported, matching do_dvrot's own restriction (it
 * disallows them outright rather than handling them via supdate()).
 */
short
do_ovrot(void)
{
	short          i, j, b;
	struct ValEnt *re, *le, *te;
	union MultiP   rp, lp, dstp, tp;
	int32_t        m, tl, idx, k, amt, t, rscan, lscan, elemsize;
	int32_t        n;	/* length of the first axis -- rotation wraps by it */

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	/* struct arrays not supported, same restriction as do_dvrot */
	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE)
		return(SET_ERR(ERR_DOMAIN));

	rscan = re->ve_length;
	lscan = le->ve_length;

	j = re->ve_rank;
	m = 1L;
	for (i = 0; i < (j - 1); i++) m *= re->ve_rho[i];
	n = j ? re->ve_rho[j - 1] : 1L;

	elemsize = re->ve_vtype & BYTES_MASK;
	tl = rscan - 1;

	b = re->ve_rank + le->ve_rank;
	VTEMP = (ws_off_t*)0;
	te = make_temp((short)re->ve_vtype, b, rscan * lscan);
	if (!te) return(SET_ERR(ERR_WSFULL));

	/* these may have moved during allocation */
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	/* result shape = (⍴L),(⍴R) -- same fill order used by do_ohrot and
	   the generic outer-product path in do_dyop() */
	i = le->ve_rank;
	while (i) te->ve_rho[--b] = le->ve_rho[--i];
	i = re->ve_rank;
	while (i) te->ve_rho[--b] = re->ve_rho[--i];

	lp.cp   = DATA_AD(le);
	dstp.cp = DATA_AD(te);

	for (k = 0; k < lscan; k++) {
		switch (le->ve_vtype & BYTES_MASK) {
			case 1:
				amt = (int32_t)(signed char)*lp.cp;
				lp.cp++;
				break;
			case 2:
				amt = (int32_t)*lp.sp;
				lp.sp++;
				break;
			case 4:
				amt = (int32_t)*lp.lp;
				lp.lp++;
				break;
			case FLOATSIZE:
			{
				/* V74: reduce by the first axis's length before
				   converting, as in do_ohrot() */
				double d = *lp.dp++;
				d = (n > 0) ? fmod(d, (double)n) : 0.0;
				if (!dbl_to_i32(d, &amt)) return(SET_ERR(ERR_DOMAIN));
				break;
			}
			default:		/* not a valid element size */
				amt = 0;
				break;
		}
		/* V74: reduce integer amounts too, so m * amt below can't
		   overflow (it's now under rscan) and the wrap loops run at
		   most once */
		if (n > 0) amt %= n;
		else amt = 0;

		rp.cp = DATA_AD(re);
		tp    = dstp;	/* same block base, viewed through every pointer type */

		for (idx = 0; idx < rscan; idx++) {
			t = m * amt;
			t = (0L - t) + idx;
			while (t > tl) t -= tl + 1;
			while (t < 0L) t += tl + 1;
			switch (elemsize) {
				case 1:
					*(tp.cp + t) = *rp.cp++;
					break;
				case 2:
					*(tp.sp + t) = *rp.sp++;
					break;
				case 4:
					*(tp.lp + t) = *rp.lp++;
					break;
				case FLOATSIZE:
					*(tp.dp + t) = *rp.dp++;
					break;
			}
		}

		dstp.cp += rscan * elemsize;
	}

	/* pop everything -- same defensive convention as do_ohrot() */
	if (VTEMP) stack_pop(3);
	else stack_pop(2);

	temp_stack(te);		/* return result */
	return(0);
}

/*
 * fmt_number -- format a numeric ValEnt as a printable character string.
 *
 * Writes into caller-supplied buffer cp, which must be at least LINE_SIZE
 * bytes (LSCRATCH satisfies this everywhere it's used). Does not touch the
 * value stack -- the caller pops re and pushes the result string.
 *
 * For rank>1, mirrors vprint()'s own matrix pre-pass: every element is
 * measured first (via floatlen() for floats, so )DIGITS is honored) to
 * find one uniform column width, elements are right-justified in that
 * width, and rows are separated with embedded newlines using the same
 * row/higher-dim break bookkeeping vprint() uses for direct display. This
 * dialect has no nested/boxed array type, so a shaped char *matrix* isn't
 * representable -- embedded newlines in a flat char vector are the closest
 * equivalent, and is what makes ⍕ of a matrix look the same as displaying
 * it directly instead of collapsing to one flat, unaligned line.
 *
 * This is the shared core of both the new OP_FMT primitive (real ⍕) and
 * the pre-existing ⎕NTA quad function (sysfuncs.c case 12), factored out
 * so ⎕NTA keeps working unchanged for existing user functions.
 */
short
fmt_number(struct ValEnt *re, char *cp)
{
long   l,t,rc;
int32_t m;
short  i,j,rank,lc,w;
char  *op;
double dacc;
int dpd,wmax,dpmax,ep,use_e;
char ffmt[30];

	if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));

	rank = re->ve_rank;
	l    = (long)re->ve_length;

	w = 1;
	use_e = dpmax = wmax = 0;

	if (rank > 1)
	{
		/* pre-pass: find one uniform column width for the whole array,
		   exactly mirroring vprint()'s own rank>1 pass, so ⍕ of a
		   matrix lines up in columns the same way direct display does. */
		w = 2;
		for (t=0L;t<l;t++)
		{
			if ((re->ve_vtype & BYTES_MASK) == FLOATSIZE)
			{
				/* floatlen()'s *return value* is i+1 -- one more than
				   the trimmed string it just wrote into LINEA (e.g. it
				   returns 5 for "3.08", which is 4 chars) -- so measure
				   the actual fractional length off LINEA itself rather
				   than off the return value, or dpmax (and every
				   column's decimal count) ends up one digit too wide.
				   Same fix as vprint.c's matching prepass. */
				dacc = get_fdata(re,t);
				floatlen(dacc,-1,&dpd,&ep);
				i = (short)strlen((char *)LINEA);
				if ((i - dpd) > dpmax) dpmax = i - dpd;
				if (dpd > wmax) wmax = dpd;
				if (ep) use_e = 1;
			} else {
				m = get_data(re,t);
				i = 1;
				if (m < 0L) { i = 2; m = 0-m; }
				while (m) { m = m/10L; i++; }
			}
			if (i > w) w = i;
		}

		if (use_e)
		{
			w = wmax + dpmax + 5;
			sprintf(ffmt,"%%%d.%de",w,dpmax);	/* maximized format (E notation) */
		} else sprintf(ffmt,"%%%d.%df",wmax+dpmax+1,dpmax);	/* maximized format (non-E) */
	}

	op  = cp;
	*op = '\0';
	rc  = 0L;
	lc  = rank ? (short)re->ve_rho[0] : 1;

	for (t=0L;t<l;t++)
	{
		if ((op - cp) > (LINE_SIZE-40)) break;	/* leave headroom */

		if (rank > 1)
		{
			if ((re->ve_vtype & BYTES_MASK) == FLOATSIZE)
			{
				dacc = get_fdata(re,t);
				sprintf((char *)LINEA,ffmt,dacc);
				if (use_e) convert_numstring(LINEA,w);	/* trim/uppercase this element's E */
			} else
			{
				m = get_data(re,t);
				sprintf((char *)LINEA,"%*d",w,(int)m);
			}
			strcpy(op,(char *)LINEA);
		} else {
			if ((re->ve_vtype & BYTES_MASK) == FLOATSIZE)
			{
				dacc = get_fdata(re,t);
				floatlen(dacc,-1,&dpd,&ep);	/* writes trimmed result into LINEA */
				strcpy(op,(char *)LINEA);
			} else
				sprintf(op,"%d",(int)get_data(re,t));
		}
		op += strlen(op);

		rc++;
		lc--;

		if (!lc)
		{
			j = 1;
			if (rank)
			{
				m = re->ve_rho[0];
				lc = (short)m;
				for (i=1;i<rank;i++)
				{
					m *= re->ve_rho[i];
					if (!(rc % m)) j++;
				}
			}
			if (rc != l)
			{
				for (i=0;i<j;i++) *op++ = '\n';
				*op = '\0';
			}
		} else if (rank <= 1) {
			*op++ = ' ';	/* vector/scalar elements get a plain separator, no column width */
			*op = '\0';
		}
	}

	convert_numstring((unsigned char *)cp,0);
	return(0);
}

/*
 * prep_exec_line -- translate a character-vector ValEnt into LINEB, ready
 * for exec_lineb().
 *
 * Applies the same input translation (quad-fn names, overstrike/font
 * sequences) as any other input line, mirroring proc_quad()'s QUAD_CODE
 * case in exec.c. Does not touch the value stack -- the caller pops re
 * and calls exec_lineb() itself.
 *
 * This is the shared core of both the new OP_QX primitive (real ⍎) and
 * the pre-existing ⎕X quad function (sysfuncs.c case 25), factored out
 * so ⎕X keeps working unchanged for existing user functions.
 */
short
prep_exec_line(struct ValEnt *re)
{
char *cp;
short i;
char *tp;

	cp = (char *)LSCRATCH;
	i = (short)MIN((LINE_SIZE-1),SBOUNDS(re));
	tp = DATA_AD(re);
	strncpy(cp,tp,i);
	cp[i] = '\0';

#ifdef NO_INPUT_PROC
	alt_input_proc((UCHAR *)cp,(UCHAR *)LINEB);
#else
	input_proc((UCHAR *)cp,(UCHAR *)LINEB);
#endif
	return(0);
}

short
do_monop(UCHAR c)
{
	short          i, j, k, sdir, err;
	int32_t        l, m, rscan, rend, tot, acc, acc2 = 0, blk;
	int32_t       *lp;
	struct ValEnt *ve, *te;
	union MultiP   mp, tp;
	char          *cp;
	double dacc, dacc2;

	VRIGHT = get_vparm(0);
	if (!VRIGHT) {
		if (c == OP_BRA) {	/* naked branch */
			do {
				pop_fn();
			} while (FUNC_NUM != -1);

			Adjust_Stack();
			ERROR_CODE = 0;
			FNLINE     = -1;
			FUNC_NUM   = -1;
			ILEN       = 0;
			return(0);
		}
		return(SET_ERR(ERR_SYNTAX));
	}
	if (!*VRIGHT) return(SET_ERR(ERR_VALUE));
	ve = WSVAL_PTR(*VRIGHT);

	switch (c) {
		case OP_RHO:
			j  = ve->ve_rank;
			i  = j;
			lp = (int32_t *)LSCRATCH;
			while (i--) *lp++ = ve->ve_rho[i];
			push_val(j, 1, (int32_t *)LSCRATCH);
			swapdrop();
			break;

		case OP_IOT:
			l = ve->ve_rank;
			if (l > 1L) return(SET_ERR(ERR_RANK));

			if (!l || ve->ve_rho[0] == 1L) {
				/* scalar right arg, or a single-element vector:
				 * original flat 1-D index vector, unchanged. */
				if (l) l = ve->ve_rho[0];
				else   l = 1L;
				mp.cp = DATA_AD(ve);
				switch (ve->ve_vtype & BYTES_MASK) {
					case 1:
						l = (int32_t)(signed char)*mp.cp;
						break;
					case 2:
						l = (int32_t)*mp.sp;
						break;
					case 4:
						l = *mp.lp;
						break;
					case FLOATSIZE:
						if (!dbl_to_i32(*mp.dp, &l))
							return(SET_ERR(ERR_DOMAIN));	/* V74 */
						break;
				}
				stack_pop(1);
				if (l < 0L) return(SET_ERR(ERR_DOMAIN));
				i = BCONST_CODE;
				if (l > 127)   i = SCONST_CODE;
				if (l > 32767) i = ICONST_CODE;
				te = make_temp(i, 1, l);
				if (!te) return(SET_ERR(ERR_WSFULL));
				mp.cp = DATA_AD(te);
				j = wspace->origin;
				for (m = (int32_t)j; m < (l + (int32_t)j); m++) {
					switch (te->ve_vtype & BYTES_MASK) {
						case 1:
							*mp.cp++ = (char)m;
							break;
						case 2:
							*mp.sp++ = (short)m;
							break;
						case 4:
							*mp.lp++ = m;
							break;
					}
				}
				break;
			}

			/* general case: R is a vector of 2+ dimension sizes --
			 * generate every index tuple for a k-dimensional array
			 * of that shape, flattened into a rank-2 result: one
			 * row per tuple, shape (prod(R), k) -- this dialect has
			 * no nested/boxed array type, so the per-dimension
			 * "real APL" nested-shape-R result isn't representable;
			 * flattening to rank 2 is the closest equivalent. R[0]
			 * is slowest-varying (outermost row grouping), R[k-1]
			 * fastest. Example: ⍳2 3 with origin 1 -> shape (6,2):
			 * 1 1/1 2/1 3/2 1/2 2/2 3. */
			{
				int32_t dims[MAX_RANK], coords[MAX_RANK];
				int32_t mdim, mval, totrows, r, rem, cval;
				short   ix;

				k = (short)ve->ve_rho[0];
				if (k > MAX_RANK) return(SET_ERR(ERR_RANK));

				mp.cp = DATA_AD(ve);
				mdim = 0L;
				for (ix = 0; ix < k; ix++) {
					switch (ve->ve_vtype & BYTES_MASK) {
						case 1:
							l = (int32_t)(signed char)*mp.cp++;
							break;
						case 2:
							l = (int32_t)*mp.sp++;
							break;
						case 4:
							l = *mp.lp++;
							break;
						case FLOATSIZE:
							if (!dbl_to_i32(*mp.dp++, &l))
								return(SET_ERR(ERR_DOMAIN));	/* V74 */
							break;
					}
					if (l < 0L) return(SET_ERR(ERR_DOMAIN));
					dims[ix] = l;
					if (l > mdim) mdim = l;
				}
				stack_pop(1);

				totrows = 1L;
				for (ix = 0; ix < k; ix++) totrows *= dims[ix];

				j = wspace->origin;
				mval = mdim ? (mdim - 1L + (int32_t)j) : (int32_t)j;
				if (mval < 0L) mval = 0L;

				i = BCONST_CODE;
				if (mval > 127)   i = SCONST_CODE;
				if (mval > 32767) i = ICONST_CODE;

				te = make_temp(i, 2, totrows * (int32_t)k);
				if (!te) return(SET_ERR(ERR_WSFULL));

				/* rank 2: last axis (ve_rho[0]) = row width k,
				 * first axis (ve_rho[1]) = row count totrows --
				 * same convention as do_compx()'s rank-2 results. */
				te->ve_rho[0] = (int32_t)k;
				te->ve_rho[1] = totrows;

				mp.cp = DATA_AD(te);
				for (r = 0; r < totrows; r++) {
					rem = r;
					/* decompose r into per-axis coordinates,
					 * dims[k-1] least significant (fastest-
					 * varying row-to-row), dims[0] most. */
					for (ix = k - 1; ix >= 0; ix--) {
						if (dims[ix] > 0L) {
							coords[ix] = rem % dims[ix];
							rem = rem / dims[ix];
						} else {
							coords[ix] = 0L;
						}
					}
					for (ix = 0; ix < k; ix++) {
						cval = coords[ix] + (int32_t)j;
						switch (te->ve_vtype & BYTES_MASK) {
							case 1:
								*mp.cp++ = (char)cval;
								break;
							case 2:
								*mp.sp++ = (short)cval;
								break;
							case 4:
								*mp.lp++ = cval;
								break;
						}
					}
				}
			}
			break;

		/* scan/reduction operations */
		case OP_EXP:		/* monadic expand is scan */
			sdir = 1;		/* scan in fwd direction */
			goto scanred;
		case OP_REDUCTION:
			sdir = -1;		/* scan in reverse */
scanred:;
			c = ~MONADIC & *IP++;	/* get op we are reducing */
			j = ve->ve_rank;
			TOTSCAN = 1L;
			if (sdir == 1) k = 0;
			else           k = 1;
			for (i = k; i < j; i++) TOTSCAN *= ve->ve_rho[i];
			i = j;
			if (i) i -= k;
			te = make_temp(ve->ve_vtype, i, TOTSCAN);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve = WSVAL_PTR(*VRIGHT);
			if (j) rend = ve->ve_rho[0];
			else   rend = 1L;
			TOTSCAN = 1L;
			for (i = k; i < j; i++) {
				te->ve_rho[i - k] = ve->ve_rho[i];
				if (i) TOTSCAN *= ve->ve_rho[i];
			}
			m = TOTSCAN;
			if (j) m *= ve->ve_rho[0];
			tp.cp = DATA_AD(te);
			mp.cp = DATA_AD(ve);
			k = te->ve_vtype & BYTES_MASK;
			if (sdir == -1) {
				mp.cp += (m - 1L) * (ve->ve_vtype & BYTES_MASK);
				tp.cp += (te->ve_length - 1L) * k;
			}
			rscan   = TOTSCAN;	/* so upsize has proper TOTSCAN */
			TOTSCAN = rend;
			m       = 0L;

			if (sdir == 1) {
				/* Textbook APL scan: (f\V)[p] = f/(p^V) -- each prefix
				   folded independently, right-to-left, exactly like a
				   fresh reduce over just that prefix (seed = the
				   prefix's last element, combine leftward) -- NOT a
				   running left-to-right accumulator. For associative,
				   commutative f (+, x, ...) the two coincide, which is
				   why this wasn't caught by earlier ADD-based testing;
				   for non-associative f (-, ...) they don't: -\iota4
				   must end in the same value as -/iota4 (its last
				   prefix IS the whole array), which a running fold
				   doesn't guarantee. Each row is contiguous in ve's own
				   memory (rho[0] is fastest), so prefix elements are
				   directly indexable here -- no need for scanredf's
				   per-column accumulator-array trick. Writes still land
				   at strictly increasing absolute indices (row-major,
				   matching the output's own layout), so upsize()'s
				   contiguous-prefix contract holds exactly as before. */
				for (blk = 0; blk < rscan; blk++) {
					int32_t rowbase = blk * rend;
					for (tot = 1; tot <= rend; tot++) {
						for (l = tot - 1; l >= 0; l--) {
							switch (ve->ve_vtype & BYTES_MASK) {
								case 1:
									acc2 = (int32_t)(signed char)((char *)DATA_AD(ve))[rowbase + l];
									break;
								case 2:
									acc2 = (int32_t)((short *)DATA_AD(ve))[rowbase + l];
									break;
								case 4:
									acc2 = ((int32_t *)DATA_AD(ve))[rowbase + l];
									break;
								case FLOATSIZE:
									dacc2 = ((double *)DATA_AD(ve))[rowbase + l];
									break;
							}
							if ((ve->ve_vtype & BYTES_MASK) != FLOATSIZE)
								dacc2 = (double)acc2;
							if (l == tot - 1) {
								acc = acc2;
								dacc = dacc2;
							} else {
								/* Per-prefix fold always runs right-to-
								   left (like reduce), regardless of the
								   outer scan direction -- pass -1
								   explicitly rather than the outer
								   sdir, which stays +1 for the write
								   side below. */
								err = REDUCE_COMBINE(c, -1, ve->ve_vtype & BYTES_MASK);
								if (err) return(err);
							}
						}
						m++;
						switch (k) {
							case 1:
								if (acc > 127L || acc < -128L ||
								    dacc > 2147483647.0 || dacc < -2147483648.0) {
									acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
									if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
									ve    = WSVAL_PTR(*VRIGHT);
									te    = WSVAL_PTR(*VTEMP);
									k     = te->ve_vtype & BYTES_MASK;
									tp.cp = DATA_AD(te);
									if (k == 2) {
										tp.sp += acc2;
										*tp.sp = (short)acc;
										tp.sp++;
									} else if (k == 4) {
										tp.lp += acc2;
										*tp.lp = acc;
										tp.lp++;
									} else {	/* FLOATSIZE: beyond int32 (V75) */
										tp.dp += acc2;
										*tp.dp = dacc;
										tp.dp++;
									}
								} else {
									*tp.cp = (char)acc;
									tp.cp++;
								}
								break;
							case 2:
								if (acc > 32767 || acc < -32768 ||
								    dacc > 2147483647.0 || dacc < -2147483648.0) {
									acc2 = (int32_t)(tp.sp - (short *)DATA_AD(te));
									if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
									ve    = WSVAL_PTR(*VRIGHT);
									te    = WSVAL_PTR(*VTEMP);
									k     = te->ve_vtype & BYTES_MASK;	/* 4, or FLOATSIZE beyond int32 (V75) */
									tp.cp = DATA_AD(te);
									if (k == 4) {
										tp.lp += acc2;
										*tp.lp = acc;
										tp.lp++;
									} else {
										tp.dp += acc2;
										*tp.dp = dacc;
										tp.dp++;
									}
								} else {
									*tp.sp = (short)acc;
									tp.sp++;
								}
								break;
							case 4:
								if (dacc > 2147483647.0 || dacc < -2147483648.0) {
									acc2 = (int32_t)(tp.lp - (int32_t *)DATA_AD(te));
									if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
									ve    = WSVAL_PTR(*VRIGHT);
									te    = WSVAL_PTR(*VTEMP);
									k     = FLOATSIZE;
									tp.cp = DATA_AD(te);
									tp.dp += acc2;
									*tp.dp = dacc;
									tp.dp++;
								} else
								{
									*tp.lp = acc;
									tp.lp++;
								}
								break;
							case FLOATSIZE:
								*tp.dp = dacc;
								tp.dp++;
								break;
						}
					}
				}
				swapdrop();
				break;
			}

			/* sdir == -1: reduce (dyadic /), unchanged. */
			while (rscan) {
scantop:;
				if (TOTSCAN) {
					switch (ve->ve_vtype & BYTES_MASK) {
						case 1:
							acc2 = (int32_t)(signed char)*mp.cp;
							mp.cp += sdir;
							break;
						case 2:
							acc2 = (int32_t)*mp.sp;
							mp.sp += sdir;
							break;
						case 4:
							acc2 = *mp.lp;
							mp.lp += sdir;
							break;
						case FLOATSIZE:
							dacc2 = *mp.dp;
							mp.dp += sdir;
							break;
					}
					if ((ve->ve_vtype & BYTES_MASK) != FLOATSIZE)
						dacc2 = (double)acc2;
					if (TOTSCAN == rend) {
						acc = acc2;
						dacc = dacc2;
					} else {
						err = REDUCE_COMBINE(c, sdir, ve->ve_vtype & BYTES_MASK);
						if (err) return(err);
					}
					TOTSCAN--;
				} else {
					err = REDUCE_IDENTITY(c, ve->ve_vtype & BYTES_MASK);
					if (err) return(err);
				}
				if (TOTSCAN) goto scantop;	/* sdir==-1 always here */
				m += sdir;
				switch (k) {
					case 1:
						if (acc > 127L || acc < -128L ||
						    dacc > 2147483647.0 || dacc < -2147483648.0) {
							acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
							if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							k     = te->ve_vtype & BYTES_MASK;
							tp.cp = DATA_AD(te);
							if (k == 2) {
								tp.sp += acc2;
								*tp.sp  = (short)acc;
								tp.sp  += sdir;
							} else if (k == 4) {
								tp.lp += acc2;
								*tp.lp  = acc;
								tp.lp  += sdir;
							} else {	/* FLOATSIZE: beyond int32 (V75) */
								tp.dp += acc2;
								*tp.dp  = dacc;
								tp.dp  += sdir;
							}
						} else {
							*tp.cp  = (char)acc;
							tp.cp  += sdir;
						}
						break;
					case 2:
						if (acc > 32767 || acc < -32768 ||
						    dacc > 2147483647.0 || dacc < -2147483648.0) {
							acc2 = (int32_t)(tp.sp - (short *)DATA_AD(te));
							if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							k     = te->ve_vtype & BYTES_MASK;	/* 4, or FLOATSIZE beyond int32 (V75) */
							tp.cp = DATA_AD(te);
							if (k == 4) {
								tp.lp += acc2;
								*tp.lp  = acc;
								tp.lp  += sdir;
							} else {
								tp.dp += acc2;
								*tp.dp  = dacc;
								tp.dp  += sdir;
							}
						} else {
							*tp.sp  = (short)acc;
							tp.sp  += sdir;
						}
						break;
					case 4:
						if (dacc > 2147483647.0 || dacc < -2147483648.0) {
							acc2 = (int32_t)(tp.lp - (int32_t *)DATA_AD(te));
							if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							k     = FLOATSIZE;
							tp.cp = DATA_AD(te);
							tp.dp += acc2;
							*tp.dp  = dacc;
							tp.dp  += sdir;
						} else
						{
							*tp.lp  = acc;
							tp.lp  += sdir;
						}
						break;
					case FLOATSIZE:
						*tp.dp  = dacc;
						tp.dp  += sdir;
						break;
				}
				TOTSCAN = rend;
				rscan--;
			}
			swapdrop();
			break;

		/* scan/reduction over the first coordinate (⌿) -- same CHAR_/OP_
		   as compress/expand-first-axis (see dyop.c's OP_COMPF/OP_EXPF),
		   distinguished by the MONADIC bit the parser already sets when
		   ⌿ follows a function. Structurally the same fold as scanred
		   above, but the axis being folded is ve_rho[j-1] (APL's first
		   coordinate, per do_compx1's convention) instead of ve_rho[0],
		   so elements of one run are `blk` elements apart in memory
		   rather than contiguous. See session notes for the derivation
		   of the end-of-run repositioning step below. */
		case OP_EXPF:		/* expand-first-axis is scan-first-axis if monadic */
			sdir = 1;		/* scan in fwd direction */
			goto scanredf;
		case OP_COMPF:		/* compress-first-axis is reduction-first-axis if monadic */
			sdir = -1;		/* scan in reverse */
scanredf:;
			c = ~MONADIC & *IP++;	/* get op we are reducing/scanning with */
			j = ve->ve_rank;
			/* blk: elements between consecutive same-run elements along
			   the folded axis (the "row" of blk contiguous elements is
			   exactly what one first-axis index spans; ⌿/⍀ fold across
			   rho[j-1] copies). This is the run count either way. */
			blk = 1L;
			for (i = 0; i < j - 1; i++) blk *= ve->ve_rho[i];
			rend = (j ? ve->ve_rho[j - 1] : 1L);	/* folded axis length */
			/* Scan (sdir==1, monadic ⍀) keeps every input element --
			   result has the same shape as R (rank j, blk*rend elements).
			   Reduce (sdir==-1, monadic ⌿) collapses the folded axis --
			   result drops it (rank j-1, blk elements). Same k=0/k=1
			   distinction scanred makes above for \ vs /. */
			if (sdir == 1) k = 0;
			else           k = 1;
			i = (j ? j - k : 0);
			te = make_temp(ve->ve_vtype, i, k ? blk : blk * rend);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve = WSVAL_PTR(*VRIGHT);
			for (i = 0; i < j - 1; i++) te->ve_rho[i] = ve->ve_rho[i];
			if (!k && j) te->ve_rho[j - 1] = rend;
			tp.cp = DATA_AD(te);
			mp.cp = DATA_AD(ve);
			k = te->ve_vtype & BYTES_MASK;

			if (sdir == 1) {
			    /* Textbook APL scan: (f\V)[p] = f/(p^V), same fix as
			       scanred above -- each prefix folded independently,
			       right-to-left, not a running accumulator (which was
			       still wrong for non-associative f even after the
			       write-position/upsize fixes: it matched scanred's
			       pre-fix behavior, e.g. -⍀M's rows wouldn't reduce
			       to -⌿M in the last row). Column c's elements sit at
			       absolute indices c, c+blk, c+2*blk, ... (strided,
			       since the fold axis here is ve_rho[j-1] rather than
			       the contiguous ve_rho[0] scanred folds over), but
			       they're still directly indexable -- no need to walk
			       mp at all, so the old per-column acc_arr machinery
			       goes away entirely. Writes stay in the same
			       strictly-increasing row-major order as before
			       (outer loop over row/prefix-length, inner over
			       column), so upsize()'s contiguous-prefix contract
			       still holds. */
			    int32_t col, p, pos, idx;
			    m = 0L;
			    for (p = 1; p <= rend; p++) {
				for (col = 0; col < blk; col++) {
				    for (pos = p - 1; pos >= 0; pos--) {
					idx = col + pos * blk;
					switch (ve->ve_vtype & BYTES_MASK) {
					    case 1:
						acc2 = (int32_t)(signed char)((char *)DATA_AD(ve))[idx];
						break;
					    case 2:
						acc2 = (int32_t)((short *)DATA_AD(ve))[idx];
						break;
					    case 4:
						acc2 = ((int32_t *)DATA_AD(ve))[idx];
						break;
					    case FLOATSIZE:
						dacc2 = ((double *)DATA_AD(ve))[idx];
						break;
					}
					if ((ve->ve_vtype & BYTES_MASK) != FLOATSIZE)
					    dacc2 = (double)acc2;
					if (pos == p - 1) {
					    acc = acc2;
					    dacc = dacc2;
					} else {
					    /* per-prefix fold always right-to-
					       left, like reduce -- pass -1
					       explicitly regardless of the
					       outer sdir (used only for the
					       write side, which stays +1). */
					    err = REDUCE_COMBINE(c, -1, ve->ve_vtype & BYTES_MASK);
					    if (err) return(err);
					}
				    }
				    m++;
				    switch (k) {
					case 1:
					    if (acc > 127L || acc < -128L ||
					        dacc > 2147483647.0 || dacc < -2147483648.0) {
						acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
						if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
						ve    = WSVAL_PTR(*VRIGHT);
						te    = WSVAL_PTR(*VTEMP);
						k     = te->ve_vtype & BYTES_MASK;
						tp.cp = DATA_AD(te);
						if (k == 2) {
						    tp.sp += acc2;
						    *tp.sp = (short)acc;
						    tp.sp++;
						} else if (k == 4) {
						    tp.lp += acc2;
						    *tp.lp = acc;
						    tp.lp++;
						} else {	/* FLOATSIZE: beyond int32 (V75) */
						    tp.dp += acc2;
						    *tp.dp = dacc;
						    tp.dp++;
						}
					    } else {
						*tp.cp = (char)acc;
						tp.cp++;
					    }
					    break;
					case 2:
					    if (acc > 32767L || acc < -32768L ||
					        dacc > 2147483647.0 || dacc < -2147483648.0) {
						acc2 = (int32_t)(tp.sp - (short *)DATA_AD(te));
						if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
						ve    = WSVAL_PTR(*VRIGHT);
						te    = WSVAL_PTR(*VTEMP);
						k     = te->ve_vtype & BYTES_MASK;	/* 4, or FLOATSIZE beyond int32 (V75) */
						tp.cp = DATA_AD(te);
						if (k == 4) {
							tp.lp += acc2;
							*tp.lp = acc;
							tp.lp++;
						} else {
							tp.dp += acc2;
							*tp.dp = dacc;
							tp.dp++;
						}
					    } else {
						*tp.sp = (short)acc;
						tp.sp++;
					    }
					    break;
					case 4:
					    if (dacc > 2147483647.0 || dacc < -2147483648.0) {
						acc2 = (int32_t)(tp.lp - (int32_t *)DATA_AD(te));
						if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
						ve    = WSVAL_PTR(*VRIGHT);
						te    = WSVAL_PTR(*VTEMP);
						k     = FLOATSIZE;
						tp.cp = DATA_AD(te);
						tp.dp += acc2;
						*tp.dp = dacc;
						tp.dp++;
					    } else
					    {
						*tp.lp = acc;
						tp.lp++;
					    }
					    break;
					case FLOATSIZE:
					    *tp.dp = dacc;
					    tp.dp++;
					    break;
				    }
				}
			    }
			    swapdrop();
			    break;
			}

			/* sdir == -1: reduce-first-axis (⌿), unchanged. Output is
			   flat/contiguous (one write per completed run), so this
			   per-run strided walk already writes upsize()'s required
			   elements in front-to-back prefix order -- it never had
			   the scan branch's bug. */
			l = (blk - 1L) + (rend - 1L) * blk;
			mp.cp += l * (ve->ve_vtype & BYTES_MASK);
			tp.cp += (te->ve_length - 1L) * k;
			rscan   = blk;
			TOTSCAN = rend;
			m       = 0L;
			while (rscan) {
scantopf:;
				if (TOTSCAN) {
					switch (ve->ve_vtype & BYTES_MASK) {
						case 1:
							acc2 = (int32_t)(signed char)*mp.cp;
							mp.cp += sdir * blk;
							break;
						case 2:
							acc2 = (int32_t)*mp.sp;
							mp.sp += sdir * blk;
							break;
						case 4:
							acc2 = *mp.lp;
							mp.lp += sdir * blk;
							break;
						case FLOATSIZE:
							dacc2 = *mp.dp;
							mp.dp += sdir * blk;
							break;
					}
					if ((ve->ve_vtype & BYTES_MASK) != FLOATSIZE)
						dacc2 = (double)acc2;
					if (TOTSCAN == rend) {
						acc = acc2;
						dacc = dacc2;
					} else {
						err = REDUCE_COMBINE(c, sdir, ve->ve_vtype & BYTES_MASK);
						if (err) return(err);
					}
					TOTSCAN--;
				} else {
					err = REDUCE_IDENTITY(c, ve->ve_vtype & BYTES_MASK);
					if (err) return(err);
				}
				if (TOTSCAN) goto scantopf;	/* sdir==-1 always here */
				m += sdir;
				switch (k) {
					case 1:
						if (acc > 127L || acc < -128L ||
						    dacc > 2147483647.0 || dacc < -2147483648.0) {
							acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
							if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							k     = te->ve_vtype & BYTES_MASK;
							tp.cp = DATA_AD(te);
							if (k == 2) {
								tp.sp += acc2;
								*tp.sp  = (short)acc;
								tp.sp  += sdir;
							} else if (k == 4) {
								tp.lp += acc2;
								*tp.lp  = acc;
								tp.lp  += sdir;
							} else {	/* FLOATSIZE: beyond int32 (V75) */
								tp.dp += acc2;
								*tp.dp  = dacc;
								tp.dp  += sdir;
							}
						} else {
							*tp.cp  = (char)acc;
							tp.cp  += sdir;
						}
						break;
					case 2:
						if (acc > 32767 || acc < -32768 ||
						    dacc > 2147483647.0 || dacc < -2147483648.0) {
							acc2 = (int32_t)(tp.sp - (short *)DATA_AD(te));
							if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							k     = te->ve_vtype & BYTES_MASK;	/* 4, or FLOATSIZE beyond int32 (V75) */
							tp.cp = DATA_AD(te);
							if (k == 4) {
								tp.lp += acc2;
								*tp.lp  = acc;
								tp.lp  += sdir;
							} else {
								tp.dp += acc2;
								*tp.dp  = dacc;
								tp.dp  += sdir;
							}
						} else {
							*tp.sp  = (short)acc;
							tp.sp  += sdir;
						}
						break;
					case 4:
						if (dacc > 2147483647.0 || dacc < -2147483648.0) {
							acc2 = (int32_t)(tp.lp - (int32_t *)DATA_AD(te));
							if (!upsizef(te, acc, m, dacc)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							k     = FLOATSIZE;
							tp.cp = DATA_AD(te);
							tp.dp += acc2;
							*tp.dp  = dacc;
							tp.dp  += sdir;
						} else
						{
							*tp.lp  = acc;
							tp.lp  += sdir;
						}
						break;
					case FLOATSIZE:
						*tp.dp  = dacc;
						tp.dp  += sdir;
						break;
				}
				/* Step mp to the fold-start of the next run -- without
				   this, mp just keeps walking off the end of the
				   current run's column into unrelated memory, so every
				   run after the first reads garbage. Lost when this
				   branch was split out of the combined scan/reduce
				   loop; scan's new sequential walk doesn't need it
				   (plain ++), but reduce's strided walk still does. */
				switch (ve->ve_vtype & BYTES_MASK) {
					case 1:
						mp.cp += sdir * (1L - rend * blk);
						break;
					case 2:
						mp.sp += sdir * (1L - rend * blk);
						break;
					case 4:
						mp.lp += sdir * (1L - rend * blk);
						break;
					case FLOATSIZE:
						mp.dp += sdir * (1L - rend * blk);
						break;
				}
				TOTSCAN = rend;
				rscan--;
			}
			swapdrop();
			break;

		case OP_AB:	/* abs */
		case OP_QU:	/* query */
		case OP_MUL:	/* times (signum monadic) */
		case OP_SUB:	/* negate */
		case OP_NOT:	/* logical not */
			/* can't automatically reuse temp because resize may be necessary */
			TOTSCAN = ve->ve_length;
			j       = ve->ve_rank;
			te      = make_temp(ve->ve_vtype, j, TOTSCAN);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve = WSVAL_PTR(*VRIGHT);	/* might have moved */
			for (i = 0; i < j; i++) te->ve_rho[i] = ve->ve_rho[i];
			tot   = TOTSCAN;
			tp.cp = DATA_AD(te);
			mp.cp = DATA_AD(ve);
			k     = ve->ve_vtype & BYTES_MASK;
			i     = te->ve_vtype & BYTES_MASK;
			while (tot) {
				switch (k) {
					case 1:
						acc = (int32_t)(signed char)*mp.cp++;
						break;
					case 2:
						acc = (int32_t)*mp.sp++;
						break;
					case 4:
						acc = *mp.lp++;
						break;
					case FLOATSIZE:
						dacc = *mp.dp++;
						break;
				}
				if (k == FLOATSIZE) {
					switch (c) {
						case OP_AB:
							if (dacc < 0.0) dacc = 0.0 - dacc;
							break;
						case OP_QU:
							dacc = (double)myrnd((unsigned long)dacc);
							dacc += (double)wspace->origin;
							break;
						case OP_MUL:
							if (dacc > 0.0) dacc =  1.0;
							if (dacc < 0.0) dacc = -1.0;
							break;
						case OP_SUB:
							dacc = 0.0 - dacc;
							break;
						case OP_NOT:
							dacc = dacc ? 0.0 : 1.0;
							break;
					}
				} else {
					switch (c) {
						case OP_AB:
							if (acc < 0L) acc = 0L - acc;
							break;
						case OP_QU:
							acc = (int32_t)myrnd((unsigned long)acc);
							acc += wspace->origin;
							break;
						case OP_MUL:
							if (acc > 0L) acc =  1L;
							if (acc < 0L) acc = -1L;
							break;
						case OP_SUB:
							acc = 0L - acc;
							break;
						case OP_NOT:
							acc = acc ? 0L : 1L;
							break;
					}
				}
				switch (i) {
					case 1:
						if (acc > 127L || acc < -128L) {
							acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
							if (!upsize(te, acc, TOTSCAN - tot)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							i     = te->ve_vtype & BYTES_MASK;
							tp.cp = DATA_AD(te);
							if (i == 2) {
								tp.sp += acc2;
								*tp.sp++ = (short)acc;
							} else {
								tp.lp += acc2;
								*tp.lp++ = acc;
							}
						} else {
							*tp.cp++ = (char)acc;
						}
						break;
					case 2:
						if (acc > 32767 || acc < -32768) {
							acc2 = (int32_t)(tp.sp - (short *)DATA_AD(te));
							if (!upsize(te, acc, TOTSCAN - tot)) return(SET_ERR(ERR_WSFULL));
							ve    = WSVAL_PTR(*VRIGHT);
							te    = WSVAL_PTR(*VTEMP);
							i     = te->ve_vtype & BYTES_MASK;
							tp.cp = DATA_AD(te);
							tp.lp += acc2;
							*tp.lp++ = acc;
						} else {
							*tp.sp++ = (short)acc;
						}
						break;
					case 4:
						*tp.lp++ = acc;
						break;
					case FLOATSIZE:
						*tp.dp++ = dacc;
						break;
				}
				tot--;
			}
			swapdrop();
			break;

		case OP_DIV:	/* monadic divide (reciprocal): dividesign R is 1 divided by R */
			j  = ve->ve_rank;
			l  = ve->ve_length;
			te = make_temp(FCONST_CODE, j, l);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve = WSVAL_PTR(*VRIGHT);	/* might have moved */
			for (i = 0; i < j; i++) te->ve_rho[i] = ve->ve_rho[i];
			mp.cp = DATA_AD(ve);
			tp.cp = DATA_AD(te);
			k     = ve->ve_vtype & BYTES_MASK;
			while (l--) {
				switch (k) {
					case 1:
						dacc = (double)(signed char)*mp.cp++;
						break;
					case 2:
						dacc = (double)*mp.sp++;
						break;
					case 4:
						dacc = (double)*mp.lp++;
						break;
					case FLOATSIZE:
						dacc = *mp.dp++;
						break;
				}
				if (!dacc) return(SET_ERR(ERR_DOMAIN));	/* matches dyop.c's apply_scalar_dyop() OP_DIV zero-check */
				*tp.dp++ = 1.0 / dacc;
			}
			swapdrop();
			break;

		case OP_DNG:	/* downgrade */
			SORTDIR = 1;
			goto contsort;
		case OP_UPG:	/* upgrade */
			SORTDIR = 0;
contsort:;
			l = ve->ve_length;
			SORTSIZE = 1L;
			if (ve->ve_rank > 1) {		/* sorting an array? */
				SORTSIZE = (short)ve->ve_rho[0];
				l = l / SORTSIZE;
			}
			i = 1;
			if (l > 127L)   i = 2;
			if (l > 32767L) i = 4;
			te = make_temp(i | CONST_CODE, 1, l);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve    = WSVAL_PTR(*VRIGHT);
			tp.cp = DATA_AD(te);
			i     = te->ve_vtype & BYTES_MASK;
			m     = (int32_t)wspace->origin;
			l    += wspace->origin;
			while (m < l) {
				switch (i) {
					case 1:
						*tp.cp++ = (char)m;
						break;
					case 2:
						*tp.sp++ = (short)m;
						break;
					case 4:
						*tp.lp++ = m;
						break;
					case FLOATSIZE:	/* can't sort floats */
						return(SET_ERR(ERR_DOMAIN));
				}
				m++;
			}
			l -= wspace->origin;
			SRCSIZE = ve->ve_vtype & BYTES_MASK;
			INDSIZE = te->ve_vtype & BYTES_MASK;
			heapsort(l, (char *)&ve->ve_rho[ve->ve_rank], (char *)&te->ve_rho[te->ve_rank]);
			swapdrop();
			break;

		case OP_CE:		/* bump up a storage step */
		case OP_FL:		/* reduce to minimum storage */
			j = ve->ve_rank;
			l = ve->ve_length;
			if (c == OP_FL) {
				dacc2 = 0.0;
				acc2  = 0L;
				m     = l;
				mp.cp = DATA_AD(ve);
				i     = ve->ve_vtype & BYTES_MASK;
				while (m--) {
					switch (i) {
						case 1:
							acc = (int32_t)(signed char)*mp.cp++;
							break;
						case 2:
							acc = (int32_t)*mp.sp++;
							break;
						case 4:
							acc = *mp.lp++;
							break;
						case FLOATSIZE:
							dacc = *mp.dp++;
							break;
					}
					if (i == FLOATSIZE) {
						if (dacc > dacc2)         dacc2 = dacc;
						if ((-1.0 - dacc) > dacc2) dacc2 = -1.0 - dacc;
					} else {
						if (acc > acc2)         acc2 = acc;
						if ((-1L - acc) > acc2) acc2 = -1L - acc;
					}
				}
				/* acc2/dacc2 = largest magnitude in array */
				if (i == FLOATSIZE) {
					if (dacc2 <= INTEGER_MAX) {
						acc2 = (int32_t)dacc2;
						goto isint;
					}
					i = FCONST_CODE;
				} else {
isint:;
					i = BCONST_CODE;
					if (acc2 > 127)   i = SCONST_CODE;
					if (acc2 > 32767) i = ICONST_CODE;
				}
			} else {	/* bump up a size */
				i = ve->ve_vtype & BYTES_MASK;
				switch (i) {
					case 1:
						i = 2;
						break;
					case 2:
						i = 4;
						break;
					case 4:
						i = FLOATSIZE;
						break;
				}
				i += ve->ve_vtype & ~BYTES_MASK;
			}
			te = make_temp(i, j, l);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve    = WSVAL_PTR(*VRIGHT);
			i     = i & BYTES_MASK;
			tp.cp = DATA_AD(te);
			mp.cp = DATA_AD(ve);
			while (j--) te->ve_rho[j] = ve->ve_rho[j];
			m = l;
			j = ve->ve_vtype & BYTES_MASK;
			while (m--) {
				switch (j) {
					case 1:
						acc = (int32_t)(signed char)*mp.cp++;
						break;
					case 2:
						acc = (int32_t)*mp.sp++;
						break;
					case 4:
						acc = *mp.lp++;
						break;
					case FLOATSIZE:
						dacc = *mp.dp++;
						acc  = (int32_t)dacc;
						break;
				}
				switch (i) {
					case 1:
						*tp.cp++ = (char)acc;
						break;
					case 2:
						*tp.sp++ = (short)acc;
						break;
					case 4:
						*tp.lp++ = acc;
						break;
					case FLOATSIZE:
						if (j != FLOATSIZE) dacc = (double)acc;
						*tp.dp++ = dacc;
						break;
				}
			}
			swapdrop();
			break;

		case OP_RAV:
			j  = ve->ve_rank;
			l  = ve->ve_length;
			te = make_temp(ve->ve_vtype, 1, l);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve = WSVAL_PTR(*VRIGHT);
			l *= ve->ve_vtype & BYTES_MASK;
			memcpy((char *)&te->ve_rho[1], (char *)&ve->ve_rho[j], l);
			swapdrop();
			break;

		case OP_TRA:
			j  = ve->ve_rank;
			te = make_temp(BCONST_CODE, 1, (int32_t)j);
			if (!te) return(SET_ERR(ERR_WSFULL));
			cp = (char *)&te->ve_rho[1];
			j--;
			for (i = 0; i <= j; i++) *cp++ = (char)(wspace->origin + (j - i));
			VLEFT  = get_vparm(0);
			VRIGHT = get_vparm(1);
			/* null-check both params properly -- VRIGHT==NULL means nothing on stack */
			if (!VRIGHT || !VLEFT) return(SET_ERR(ERR_SYNTAX));
			return(do_dtrans());

		case OP_HROT:
			return(do_dhrot(0));

		case OP_VROT:
			return(do_dvrot(0));

		case OP_QDAS:
		case OP_QTAS:
			LAST_OPERATOR = OP_ASS;
			vprint((struct MemEnt *)ve);
			if (c == OP_QDAS) NL();
			break;

		case OP_EXEC:
			cp = (char *)LSCRATCH;
			l  = ve->ve_length;
			strncpy(cp, DATA_AD(ve), (int)l);
			cp[l] = '\0';
			/* x commands always lower case */
			if (wspace->caseflip) caseflip((UCHAR *)cp);
			SILENT_COMMAND = 1;
			i = do_command((UCHAR *)cp);
			SILENT_COMMAND = 0;
			stack_pop(1);
			return(i);

		case OP_FMT:	/* real Format (⍕) -- not to be confused with OP_EXEC above */
			cp = (char *)LSCRATCH;
			i = fmt_number(ve, cp);
			if (i) return(i);
			stack_pop(1);
			return(push_str(cp));

		case OP_QX:	/* real Execute (⍎) -- not to be confused with OP_EXEC above */
			prep_exec_line(ve);
			stack_pop(1);
			return(exec_lineb());

#ifdef TRIG
		case OP_POW:	/* monadic * (exponential): e to the power R */
		case OP_LOG:	/* monadic circle-star (natural log): ln R */
		case OP_OH:	/* monadic circle (pi times): R times pi */
			j  = ve->ve_rank;
			l  = ve->ve_length;
			te = make_temp(FCONST_CODE, j, l);
			if (!te) return(SET_ERR(ERR_WSFULL));
			ve    = WSVAL_PTR(*VRIGHT);	/* might have moved */
			for (i = 0; i < j; i++) te->ve_rho[i] = ve->ve_rho[i];	/* result has R's shape */
			mp.cp = DATA_AD(ve);
			tp.cp = DATA_AD(te);
			k     = ve->ve_vtype & BYTES_MASK;
			while (l--) {
				switch (k) {
					case 1:
						dacc = (double)(signed char)*mp.cp++;
						break;
					case 2:
						dacc = (double)*mp.sp++;
						break;
					case 4:
						dacc = (double)*mp.lp++;
						break;
					case FLOATSIZE:
						dacc = *mp.dp++;
						break;
				}
				if (c == OP_POW) {
					dacc = exp(dacc);
					/* overflow (R above about 709.78) gives infinity:
					   domain error, as APL2/Dyalog do for *1000 */
					if (!isfinite(dacc)) return(SET_ERR(ERR_DOMAIN));
					*tp.dp++ = dacc;
				}
				else if (c == OP_LOG) {
					/* real logs only: zero and negatives have none */
					if (dacc <= 0.0) return(SET_ERR(ERR_DOMAIN));
					*tp.dp++ = log(dacc);
				}
				else *tp.dp++ = dacc * PI;
			}
			swapdrop();
			break;
#endif

		case OP_BRA:
			LAST_OPERATOR = OP_ASS;		/* branch doesn't output */
			if (ve->ve_rank == 1 && !ve->ve_rho[0]) {
				stack_pop(1);	/* don't branch on 0p */
				break;
			}
			mp.cp = DATA_AD(ve);
			switch (ve->ve_vtype & BYTES_MASK) {
				case 1:
					l = (int32_t)(signed char)*mp.cp;
					break;
				case 2:
					l = (int32_t)*mp.sp;
					break;
				case 4:
					l = *mp.lp;
					break;
				case FLOATSIZE:
				{
					/* V74: clamp before converting (undefined for
					   an out-of-range double); the clamp below then
					   applies as for an integer target */
					double d = *mp.dp;
					if (d != d) return(SET_ERR(ERR_DOMAIN));	/* NaN */
					if (d > 32767.0) d = 32767.0;
					if (d < -32768.0) d = -32768.0;
					l = (int32_t)d;
					break;
				}
				default:		/* not a valid element size */
					l = 0;
					break;
			}
			/* V74: clamp to NEXTLINE's short range. (short)l used to
			   wrap, so in a short function ->65541 went to line 5
			   (confirmed by the person). A function has at most
			   4095 lines (FLINE_MASK), so 32767 is past the end of
			   any of them and leaves the function, as any target
			   past the last line does; a large negative target
			   stays negative, so whatever a negative branch does is
			   unchanged. */
			if (l > 32767L) l = 32767L;
			if (l < -32768L) l = -32768L;
			stack_pop(1);
			NEXTLINE = (short)l;		/* branch to here */
			if (FUNC_NUM == -1) {
				/* At the prompt: ->0 resumes the innermost suspended
				   function where it stopped (after an error, reruns
				   that line); ->N resumes it at the start of line N,
				   leaving it if N is past the end. With nothing
				   suspended, ->N does nothing. */
				if (l < 0) return(SET_ERR(ERR_DOMAIN));
				if (l > 0 && !resume_line(l)) break;
				end_fn();
				/* nowhere to go is OK */
				if (ERROR_CODE == ERR_SYSTEM) ERROR_CODE = 0;
				/* ->0 resumed a suspended function: )STEP applies
				   from here until the next typed line */
				if (FUNC_NUM != -1) step_resumed = 1;
			}
			break;

		default:
			return(SET_ERR(ERR_SYNTAX));	/* undefined symbol */
	}
	return(0);
}
