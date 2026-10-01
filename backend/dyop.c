/*
	dyop.c
*/

#define DYOP_C 1

#include "parse.h"
#include "mplmem.h"
#include "externs.h"

/* file-local helper: cast ws_off_t offset to struct ValEnt* */
#define WSVAL_PTR(off) ((struct ValEnt *)WS_PTR(off))

struct TakeScan {
	int32_t istart,ostart;
	int32_t iskip,oskip;
	int32_t iscan,oscan;
};

short
do_dtrans()
{
short b,i,j,k,s;
long lscan,acc;
struct ValEnt *te,*re,*le;
union MultiP rs,ts;
char *cp;
short *sp;
int32_t *lp;
char *rho_tarray;
int32_t *rho_larray;
double *dp;
	
	rho_tarray = BSCRATCH;
	rho_larray = LSCRATCH;
	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));

	if (le->ve_rank != 1) return(SET_ERR(ERR_RANK));
	b = (short)le->ve_rho[0];	
	if (b != re->ve_rank) return(SET_ERR(ERR_RANK));

/* move left vect into rho_tarray */
	k = le->ve_vtype & BYTES_MASK;
	ts.cp = DATA_AD(le);
	for (j=0;j<b;j++) 
	{
		switch (k) 
		{
#ifdef BINS
			case 0:
				acc = 1 & (long)(*(ts.cp + (j >> 3)) >> (j & 7));
				break;
#endif
			case 1:
				acc = (long)(signed char)*(ts.cp + j);
				break;
			case 2:
				acc = (long)*(ts.sp + j);
				break;
			case 4:
				acc = *(ts.lp + j);
				break;
			case FLOATSIZE:
			{
				int32_t a32;
				if (!dbl_to_i32(*(ts.dp + j), &a32))
					return(SET_ERR(ERR_DOMAIN));	/* V74 */
				acc = a32;
				break;
			}
			default:		/* not a valid element size */
				acc = 0;
				break;
		}
		acc -= wspace->origin;
		/* each axis must name one of R's b axes (V74): an out-of-range
		   one was stored through (char) and then used to index
		   rho_larray[], which only has b entries set -- a negative one
		   wrote before the start of LSCRATCH */
		if (acc < 0 || acc >= b) return(SET_ERR(ERR_DOMAIN));
		rho_tarray[j] = (char)acc;
	}

/* compute result rank and size */
	b--;
	k = 0;
	for (i=0;i<=b;i++) rho_larray[i] = -1L;
	for (i=0;i<=b;i++) 
	{
		j = rho_tarray[i];
		if (rho_larray[j] == -1L) rho_larray[j] = re->ve_rho[b-i];
		else {
			if (rho_larray[j] > re->ve_rho[b-i]) rho_larray[j] = re->ve_rho[b-i];
		}
		if (k < j) k = j;
	}
	lscan = 1L;
	for (i=0;i<=k;i++) 
	{
		if (rho_larray[i] == -1L) return(SET_ERR(ERR_DOMAIN));
		lscan *= rho_larray[i];
	}

	te = make_temp((short)re->ve_vtype,k+1,lscan);
	if (!te) return(SET_ERR(ERR_WSFULL));	/* out of memory */

	re = WSVAL_PTR(*VRIGHT);	/* may have moved */

	for (i=0;i<=k;i++) 
	{
		te->ve_rho[k-i] = rho_larray[i];
		rho_larray[i] = 0L;
	}

	ts.cp = DATA_AD(te);
	rs.cp = DATA_AD(re);
	s = re->ve_vtype & BYTES_MASK;	/* these'll both be the same */
	while (lscan) 
	{
		acc = 0L;
		for (i=0;i<=b;i++) 
		{
			if (acc) acc *= re->ve_rho[b - i];	/* weighting */
			acc += rho_larray[k - rho_tarray[i]];
		}
		switch (s) {
#ifdef BINS
			case 0:
				cp = rs.cp;
				cp += acc >> 3;
				*ts.cp++ = 1 & (*cp >> (acc & 7));
				break;
#endif
			case 1:
				cp = rs.cp;
				cp += acc;
				*ts.cp++ = *cp;
				break;
			case 2:
				sp = rs.sp;
				sp += acc;
				*ts.sp++ = *sp;
				break;
			case 4:
				lp = rs.lp;
				lp += acc;
				*ts.lp++ = *lp;
				break;
			case FLOATSIZE:
				dp = rs.dp;
				dp += acc;
				*ts.dp++ = *dp;
				break;
		}
		j = 0;
		while (++rho_larray[j] == te->ve_rho[j]) rho_larray[j++] = 0L;
		lscan--;
	}
/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

short
do_drav()
{
short b,i,j;
long lscan,lend,rend,rscan,acc = 0;
long rtend,rtscan,ltend,ltscan;
struct ValEnt *te,*re,*le;
union MultiP ls,rs,ts;
int32_t *rho_array;
short structflag,flt = -1;	/* -1: no floatv allocated */
struct ValEnt *tmp;
long ttscan;
short srctype;	/* type of whichever side was actually read this pass --
		   i/j are the LEFT/RIGHT types and stay fixed for the
		   whole loop, so they can't be used to tell which side
		   fed acc/dacc on a given iteration. Assigned every pass
		   since it's cheap and keeps the
		   read-side switches symmetric either way. */
double dacc = 0.0;

	rho_array = LSCRATCH;
	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) 
	{
		if ((le->ve_vtype | VARIABLE_BIT) != STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));
		structflag = 1;
	} else {
		if ((le->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));
		structflag = 0;
	}

	i = re->ve_rank;
	j = le->ve_rank;
	rho_array[0] = 1L;
	ltend = 1L;
	if (j && i <= j) ltend = le->ve_rho[0];
	rtend = 1L;
	if (i && j <= i) rtend = re->ve_rho[0];
	if (i > j) 
	{ 
		for (b=0;b<i;b++) rho_array[b] = re->ve_rho[b];
		if (!b) b = 1;
		rho_array[0] += ltend;
	} else {
		for (b=0;b<j;b++) rho_array[b] = le->ve_rho[b];
		if (!b) b = 1;
		rho_array[0] += rtend;
	}
	TOTSCAN = 1L;
	for (i=0;i<b;i++) TOTSCAN *= rho_array[i];

	rtend = re->ve_length;
	ltend = le->ve_length;

	i = le->ve_vtype & BYTES_MASK;
	j = re->ve_vtype & BYTES_MASK;
	if (i == j && (re->ve_vtype | VARIABLE_BIT) == QUOT_CODE) 
	{
		i = re->ve_vtype;
	} else i = (le->ve_vtype & ~BYTES_MASK) | MAX(i,j);

	te = make_temp(i,b,TOTSCAN);
	if (!te) return(SET_ERR(ERR_WSFULL));	/* out of memory */

	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);
	te->ve_vtype = (unsigned char)i;
	for (i=0;i<b;i++) te->ve_rho[i] = rho_array[i];

	rend = 1L;
	lend = 1L;
	i = re->ve_rank;
	j = le->ve_rank;
	if (i && j <= i) rend = re->ve_rho[0];
	if (j && i <= j) lend = le->ve_rho[0];

	ls.cp = DATA_AD(le);
	rs.cp = DATA_AD(re);
	ts.cp = DATA_AD(te);
	b = te->ve_vtype & BYTES_MASK;
	i = le->ve_vtype & BYTES_MASK;
	j = re->ve_vtype & BYTES_MASK;

	if (structflag) 
	{
		memset(ts.cp,0,TOTSCAN * b);
		flt = AllocFloatv();
		if (flt == -1) return(SET_ERR(ERR_NOFLOATV));
		ttscan = 0L;
	}
	
	ltscan = rtscan = lscan = rscan = 0L;
	while (TOTSCAN--) 
	{
		if (lscan < lend) 
		{
			switch (i) 
			{
				case 1:
					acc = (long)(signed char)*ls.cp++;
					break;
				case 2:
					acc = (long)*ls.sp++;
					break;
				case 4:
					acc = *ls.lp++;
					break;
				case FLOATSIZE:
					dacc = *ls.dp++;
					break;
			}
			srctype = i;
			lscan++;
			if (++ltscan == ltend) 
			{
				ltscan = 0L;
				ls.cp = DATA_AD(le);
			}
		} else {
			switch (j) 
			{
				case 1:
					acc = (long)(signed char)*rs.cp++;
					break;
				case 2:
					acc = (long)*rs.sp++;
					break;
				case 4:
					acc = *rs.lp++;
					break;
				case FLOATSIZE:
					dacc = *rs.dp++;
					break;
			}
			srctype = j;
			if (++rscan == rend) rscan = lscan = 0;
			if (++rtscan == rtend) 
			{
				rtscan = 0L;
				rs.cp = DATA_AD(re);
			}
		}
		switch (b) 
		{
			case 1:
				*ts.cp++ = (char)acc;
				break;
			case 2:
				*ts.sp++ = (short)acc;
				break;
			case 4:
				if (structflag && acc) 
				{
					FLOATVS[flt] = (ws_off_t)acc;
					tmp = dup_val(&FLOATVS[flt]);
					re = WSVAL_PTR(*VRIGHT);
					le = WSVAL_PTR(*VLEFT);
					te = WSVAL_PTR(*VTEMP);
					ls.cp = DATA_AD(le);
					rs.cp = DATA_AD(re);
					ts.cp = DATA_AD(te);
					ls.lp += ltscan;
					rs.lp += rtscan;
					ts.lp += ttscan;
					ttscan++;
					tmp->ve_reference = WS_OFF(ts.lp);
					debug_check_ref("dyop.c/ts.lp", (struct MemEnt*)tmp, tmp->ve_reference); /* %%% DBG */
					acc = (int32_t)WS_OFF(tmp);
				}
				*ts.lp++ = acc;
				break;
			case FLOATSIZE:
				if (srctype != FLOATSIZE) dacc = acc;
				*ts.dp++ = dacc;
				break;
		}
	}

/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	if (structflag) 
	{
		FLOATVS[flt] = (ws_off_t)0;
	}
	return(0);
}

/*
	first-axis dyadic catenate ( L CATF R ), glyph 0xAC (comma overstruck
	with a horizontal bar) -- CHAR_CATF/OP_CATF.

	Plain dyadic catenate (do_drav(), just above) joins along axis 0 --
	the innermost/fastest-varying dimension in this codebase's rho[]
	ordering (see the shape-array comment on struct ValEnt in parse.h),
	which for a rank-2 value is the *columns*. This is the same
	operation, but joins along the outermost/slowest-varying dimension
	instead -- rho[rank-1] -- which for a rank-2 value is the *rows*: L
	and R are stacked one on top of the other rather than side by side.

	Because rho[0] is the fastest-varying axis, a "row" (one full slice
	along the axis being grown here) is already contiguous in memory --
	unlike do_drav()'s per-row interleave of individual scalars, this
	function only ever needs to emit two contiguous runs: all of L's
	data, immediately followed by all of R's data (with element-by-
	element type promotion when L and R differ in width, same as
	do_drav()). No cyclic re-reading of either side is needed here the
	way do_drav() needs it for its column-broadcast case: do_drav() may
	have to repeat a lower-rank operand's contribution across many
	*separate* output rows (hence its ltscan/rtscan wraparound), but
	here the axis being grown IS the outermost one, so there's only ever
	a single row-group to fill -- a lower-rank operand contributes its
	data exactly once, laminated on as one extra "row", never repeated.

	Rank/shape handling mirrors do_drav()'s exactly, just checked against
	each operand's *own* outermost axis (ve_rho[rank-1]) instead of its
	innermost one (ve_rho[0]), and the growing slot in the result shape
	is rho_array[b-1] instead of rho_array[0]. Same conventions carry
	over: when L and R have equal rank, each contributes its own real
	outermost-axis size (a genuine row-count sum); when one side has
	strictly lower rank, that side always contributes exactly one
	lamination unit regardless of its own shape, matching do_drav()'s
	existing comma semantics for rank-mismatched arguments. As with
	do_drav(), the non-growing axes are trusted to already agree between
	L and R (or to be one-element/scalar-broadcastable) -- no defensive
	check that they do, same laxness do_drav() itself has.

	Struct-array handling (the FLOATVS/dup_val indirection under case 4)
	is carried over unchanged from do_drav(), adjusted only to track
	absolute elements-consumed-so-far from L and R (lconsumed/rconsumed)
	instead of do_drav()'s mod-boundary ltscan/rtscan, since there's no
	wraparound to track here.
*/
short
do_drav1()
{
short b,i,j;
long acc = 0;
long ltend,rtend;
long blksize,lscan,rscan,lconsumed,rconsumed;
struct ValEnt *te,*re,*le;
union MultiP ls,rs,ts;
int32_t *rho_array;
short structflag,flt = -1;	/* -1: no floatv allocated */
struct ValEnt *tmp;
long ttscan;
short srctype;	/* type of whichever side was actually read this pass,
		   same reasoning as do_drav()'s srctype */
double dacc = 0.0;

	rho_array = LSCRATCH;
	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) 
	{
		if ((le->ve_vtype | VARIABLE_BIT) != STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));
		structflag = 1;
	} else {
		if ((le->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));
		structflag = 0;
	}

	i = re->ve_rank;
	j = le->ve_rank;
	rho_array[0] = 1L;	/* base value for the both-scalar case, where
				   the copy loop below never runs */
	ltend = 1L;
	if (j && i <= j) ltend = le->ve_rho[j-1];
	rtend = 1L;
	if (i && j <= i) rtend = re->ve_rho[i-1];
	if (i > j) 
	{ 
		for (b=0;b<i;b++) rho_array[b] = re->ve_rho[b];
		if (!b) b = 1;
		rho_array[b-1] += ltend;
	} else {
		for (b=0;b<j;b++) rho_array[b] = le->ve_rho[b];
		if (!b) b = 1;
		rho_array[b-1] += rtend;
	}

	blksize = 1L;	/* elements in one "row" -- every axis except the
			   growing outermost one */
	for (i=0;i<b-1;i++) blksize *= rho_array[i];
	TOTSCAN = blksize * rho_array[b-1];

	i = le->ve_vtype & BYTES_MASK;
	j = re->ve_vtype & BYTES_MASK;
	if (i == j && (re->ve_vtype | VARIABLE_BIT) == QUOT_CODE) 
	{
		i = re->ve_vtype;
	} else i = (le->ve_vtype & ~BYTES_MASK) | MAX(i,j);

	te = make_temp(i,b,TOTSCAN);
	if (!te) return(SET_ERR(ERR_WSFULL));	/* out of memory */

	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);
	te->ve_vtype = (unsigned char)i;
	for (i=0;i<b;i++) te->ve_rho[i] = rho_array[i];

	/* re-derive each side's own outermost-axis block count against the
	   now-current le/re -- same rule as the shape calc above */
	i = re->ve_rank;
	j = le->ve_rank;
	rtend = 1L;
	if (i && j <= i) rtend = re->ve_rho[i-1];
	ltend = 1L;
	if (j && i <= j) ltend = le->ve_rho[j-1];

	ls.cp = DATA_AD(le);
	rs.cp = DATA_AD(re);
	ts.cp = DATA_AD(te);
	b = te->ve_vtype & BYTES_MASK;
	i = le->ve_vtype & BYTES_MASK;
	j = re->ve_vtype & BYTES_MASK;

	if (structflag) 
	{
		memset(ts.cp,0,TOTSCAN * b);
		flt = AllocFloatv();
		if (flt == -1) return(SET_ERR(ERR_NOFLOATV));
		ttscan = 0L;
	}

	lscan = ltend * blksize;	/* elements still owed from L */
	rscan = rtend * blksize;	/* elements still owed from R */
	lconsumed = rconsumed = 0L;
	while (TOTSCAN--) 
	{
		if (lscan) 
		{
			lscan--;
			switch (i) 
			{
				case 1:
					acc = (long)(signed char)*ls.cp++;
					break;
				case 2:
					acc = (long)*ls.sp++;
					break;
				case 4:
					acc = *ls.lp++;
					break;
				case FLOATSIZE:
					dacc = *ls.dp++;
					break;
			}
			srctype = i;
			lconsumed++;
		} else {
			rscan--;
			switch (j) 
			{
				case 1:
					acc = (long)(signed char)*rs.cp++;
					break;
				case 2:
					acc = (long)*rs.sp++;
					break;
				case 4:
					acc = *rs.lp++;
					break;
				case FLOATSIZE:
					dacc = *rs.dp++;
					break;
			}
			srctype = j;
			rconsumed++;
		}
		switch (b) 
		{
			case 1:
				*ts.cp++ = (char)acc;
				break;
			case 2:
				*ts.sp++ = (short)acc;
				break;
			case 4:
				if (structflag && acc) 
				{
					FLOATVS[flt] = (ws_off_t)acc;
					tmp = dup_val(&FLOATVS[flt]);
					re = WSVAL_PTR(*VRIGHT);
					le = WSVAL_PTR(*VLEFT);
					te = WSVAL_PTR(*VTEMP);
					ls.cp = DATA_AD(le);
					rs.cp = DATA_AD(re);
					ts.cp = DATA_AD(te);
					ls.lp += lconsumed;
					rs.lp += rconsumed;
					ts.lp += ttscan;
					ttscan++;
					tmp->ve_reference = WS_OFF(ts.lp);
					debug_check_ref("dyop.c/ts.lp", (struct MemEnt*)tmp, tmp->ve_reference); /* %%% DBG */
					acc = (int32_t)WS_OFF(tmp);
				}
				*ts.lp++ = acc;
				break;
			case FLOATSIZE:
				if (srctype != FLOATSIZE) dacc = acc;
				*ts.dp++ = dacc;
				break;
		}
	}

/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	if (structflag) 
	{
		FLOATVS[flt] = (ws_off_t)0;
	}
	return(0);
}

/*
	outer product catenate ( L o.,  R )

	Not the same operation as plain dyadic catenate (do_drav()) rank-
	promoted along one axis -- under outer product every scalar of L is
	paired with every scalar of R, and each pair is catenated into its
	own 2-element result. Result rank is (rank L)+(rank R)+1, result
	shape is (rho L),(rho R),2 -- that trailing 2-axis is the "invisible"
	axis that collapses away in the generic outer-product scalar case
	(where f(l,r) is itself a scalar), but here f(l,r) is a 2-vector.

	Char vs numeric mixing is a domain error (matches the decision that
	plain , permits it but o., shouldn't, since each output pair stands
	alone rather than joining into one coherent array). Mixed numeric
	types (short/int/float) are permitted and promote to the wider of
	the two, same MAX-width convention used elsewhere in this file.
*/
short
do_ocat()
{
short b,i,j,csize,tt;
long lscan,rscan,ltotal,rtotal;
struct ValEnt *te,*re,*le;
union MultiP lp,rp,tp;
long acc = 0,acc2 = 0;
double dacc = 0.0,dacc2 = 0.0;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	/* char/numeric mixing not allowed under outer catenate */
	if (((le->ve_vtype | VARIABLE_BIT) == QUOT_CODE) !=
	    ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE))
		return(SET_ERR(ERR_DOMAIN));

	if ((le->ve_vtype | VARIABLE_BIT) == QUOT_CODE) {
		csize = 1;
		tt = QUOT_CODE;
	} else {
		i = le->ve_vtype & BYTES_MASK;
		j = re->ve_vtype & BYTES_MASK;
		csize = i > j ? i : j;
		tt = CONST_CODE | csize;
	}

	ltotal = le->ve_length;
	rtotal = re->ve_length;

	b = le->ve_rank + re->ve_rank + 1;
	TOTSCAN = ltotal * rtotal * 2L;

	te = make_temp((short)tt,b,TOTSCAN);
	if (!te) return(SET_ERR(ERR_WSFULL));	/* out of memory */
	re = WSVAL_PTR(*VRIGHT);	/* may have moved while we alloced */
	le = WSVAL_PTR(*VLEFT);
	te->ve_vtype = (unsigned char)tt;

	i = b;
	j = le->ve_rank;
	while (j) te->ve_rho[--i] = le->ve_rho[--j];
	j = re->ve_rank;
	while (j) te->ve_rho[--i] = re->ve_rho[--j];
	te->ve_rho[--i] = 2;	/* trailing pair-axis: lowest storage index = last display axis */

	i = le->ve_vtype & BYTES_MASK;
	j = re->ve_vtype & BYTES_MASK;
	b = te->ve_vtype & BYTES_MASK;	/* output element byte size (csize) */

	lp.cp = DATA_AD(le);
	tp.cp = DATA_AD(te);

	for (lscan=0;lscan<ltotal;lscan++)
	{
		switch (i) {	/* fetch one L scalar */
			case 1:
				acc = (long)(signed char)*lp.cp;
				break;
			case 2:
				acc = (long)*lp.sp;
				break;
			case 4:
				acc = *lp.lp;
				break;
			case FLOATSIZE:
				dacc = *lp.dp;
				break;
		}

		rp.cp = DATA_AD(re);	/* R sweeps fully for every L element */
		for (rscan=0;rscan<rtotal;rscan++)
		{
			switch (j) {	/* fetch one R scalar */
				case 1:
					acc2 = (long)(signed char)*rp.cp;
					break;
				case 2:
					acc2 = (long)*rp.sp;
					break;
				case 4:
					acc2 = *rp.lp;
					break;
				case FLOATSIZE:
					dacc2 = *rp.dp;
					break;
			}

			switch (b) {	/* write L's value, promoted to output width */
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
					*tp.dp++ = (i == FLOATSIZE) ? dacc : (double)acc;
					break;
			}
			switch (b) {	/* write R's value, promoted to output width */
				case 1:
					*tp.cp++ = (char)acc2;
					break;
				case 2:
					*tp.sp++ = (short)acc2;
					break;
				case 4:
					*tp.lp++ = acc2;
					break;
				case FLOATSIZE:
					*tp.dp++ = (j == FLOATSIZE) ? dacc2 : (double)acc2;
					break;
			}

			switch (j) {	/* advance R pointer */
				case 1:
					rp.cp++;
					break;
				case 2:
					rp.sp++;
					break;
				case 4:
					rp.lp++;
					break;
				case FLOATSIZE:
					rp.dp++;
					break;
			}
		}

		switch (i) {	/* advance L pointer, once per full R sweep */
			case 1:
				lp.cp++;
				break;
			case 2:
				lp.sp++;
				break;
			case 4:
				lp.lp++;
				break;
			case FLOATSIZE:
				lp.dp++;
				break;
		}
	}

	stack_pop(3);
	temp_stack(te);
	return(0);
}

/*
	elem_d() -- element i of a value's data as a double, whatever its
	element size (t = ve_vtype & BYTES_MASK). Decode and encode mix
	element sizes between their arguments and their result, so they
	read through this rather than a pointer of the result's type.
*/
static double
elem_d(union MultiP p, short t, long i)
{
	switch (t) {
		case 1: return((double)(signed char)p.cp[i]);
		case 2: return((double)p.sp[i]);
		case 4: return((double)p.lp[i]);
		case FLOATSIZE: return(p.dp[i]);
	}
	return(0.0);
}

/*
	all_whole() -- 1 if every element of ve is a whole number: always
	true for the integer types, checked element by element for float.
	Lets decode and encode keep integer arithmetic (and an integer
	result, as before) for float radices like 10.0, and go to float only
	for genuinely fractional ones like 1.5.
*/
static int
all_whole(struct ValEnt *ve)
{
long k;
union MultiP p;

	if ((ve->ve_vtype & BYTES_MASK) != FLOATSIZE) return(1);
	p.cp = DATA_AD(ve);
	for (k=0;k<ve->ve_length;k++)
		if (p.dp[k] != floor(p.dp[k])) return(0);
	return(1);
}

/*
	encode (dyadic ⊤): L⊤R. The result's shape is (⍴L),(⍴R), laid out
	radix-position-major: for each position in L, that digit of every
	element of R.

	Follows APL's definition, working right to left through the radices:
	each digit is the floor-based residue r|n, then n <- (n - digit) / r;
	a zero radix takes all of what's left of n, leaving 0 for the
	positions to its left. That handles negative numbers (10 10⊤¯1 is
	9 9), zero radices (10 0⊤123 is 0 123) and -- in float -- fractional
	values and non-integer radices (10 10⊤12.5 is 1 2.5).

	The result is float if R is float or any radix isn't a whole number.
	Otherwise it's integer, sized to hold both R's values and the largest
	radix, since a residue can exceed R (200⊤¯1 is 199). R is read by its
	own element size and the result written by the result's, which may
	differ.

	Named do_denc since V66; it was do_ddec (and ⊤ was OP_DEC) before the
	decode/encode names were swapped to match what they do.
*/
short
do_denc()
{
short b,i,j;
short tt,lt,rt;
long lend,rend,k,p;
double bound,v,n,r,d;
int64_t in,ir,id;
struct ValEnt *te,*re,*le;
union MultiP ls,rs,ts;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	b = le->ve_rank;

	/* calc size of left side */
	lend = 1;
	for (i=0;i<b;i++) lend *= le->ve_rho[i];

	/* calc size of right side */
	j = re->ve_rank;
	rend = 1;
	for (i=0;i<j;i++) rend *= re->ve_rho[i];

	lt = le->ve_vtype & BYTES_MASK;
	rt = re->ve_vtype & BYTES_MASK;

	/* result element size: float, or the smallest integer size that
	   holds every |R| and every |radix| */
	if (rt == FLOATSIZE || !all_whole(le)) tt = FLOATSIZE;
	else
	{
		ls.cp = DATA_AD(le);
		rs.cp = DATA_AD(re);
		bound = 1.0;
		for (k=0;k<lend;k++)
		{
			v = fabs(elem_d(ls,lt,k));
			if (v > bound) bound = v;
		}
		for (k=0;k<rend;k++)
		{
			v = fabs(elem_d(rs,rt,k));
			if (v > bound) bound = v;
		}
		if (bound > 2147483647.0) tt = FLOATSIZE;
		else if (bound > 32767.0) tt = 4;
		else if (bound > 127.0) tt = 2;
		else tt = 1;
	}
	if ((re->ve_vtype & BYTES_MASK) > tt) tt = re->ve_vtype;
	else tt = tt | (re->ve_vtype & ~BYTES_MASK);

	te = make_temp(tt,j+b,lend*rend);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);	/* may have moved, re-get them */
	le = WSVAL_PTR(*VLEFT);

	/* build temp's rho */
	i = te->ve_rank;
	while (b) te->ve_rho[--i] = le->ve_rho[--b];
	while (j) te->ve_rho[--i] = re->ve_rho[--j];

	rs.cp = DATA_AD(re);
	ls.cp = DATA_AD(le);
	ts.cp = DATA_AD(te);
	tt = te->ve_vtype & BYTES_MASK;

	/* NO MORE MEMORY ALLOCS TIL COMPLETION */

	for (k=0;k<rend;k++)		/* each number in R */
	{
		if (tt == FLOATSIZE)
		{
			n = elem_d(rs,rt,k);
			for (p=lend-1;p>=0;p--)
			{
				r = elem_d(ls,lt,p);
				if (r == 0.0)
				{
					d = n;			/* zero radix takes the rest */
					n = 0.0;
				}
				else
				{
					d = n - r * floor(n / r);	/* r|n */
					n = (n - d) / r;
				}
				ts.dp[p*rend + k] = d;
			}
		}
		else
		{
			/* whole numbers within the result's integer range: exact
			   in 64-bit arithmetic, on every platform */
			in = (int64_t)elem_d(rs,rt,k);
			for (p=lend-1;p>=0;p--)
			{
				ir = (int64_t)elem_d(ls,lt,p);
				if (ir == 0)
				{
					id = in;		/* zero radix takes the rest */
					in = 0;
				}
				else
				{
					/* r|n, floor-based: C's % truncates toward zero,
					   so adjust when the signs differ */
					id = in % ir;
					if (id != 0 && ((id < 0) != (ir < 0))) id += ir;
					in = (in - id) / ir;
				}
				switch (tt)
				{
					case 1: ts.cp[p*rend + k] = (char)id; break;
					case 2: ts.sp[p*rend + k] = (short)id; break;
					case 4: ts.lp[p*rend + k] = (int32_t)id; break;
				}
			}
		}
	}

	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

/*
	decode (dyadic ⊥): L⊥R, the base value of R's digits in radices L.
	Contracts L against R's LAST axis; a scalar or one-element argument
	on either side is extended (see the block just below). Weights are
	built in double and the result sized up front from (largest |digit|)
	x (sum of |weights|), so no term can overflow and a zero radix never
	divides -- 10 0⊥3 4 is 4. A fractional radix, or a result too large
	for int32, gives a float result.

	Named do_ddec since V66; it was do_denc (and ⊥ was OP_ENC) before the
	decode/encode names were swapped to match what they do. Test:
	tests/decode_encode.apl.
*/
short
do_ddec()
{
short b,i,j;
short tt,lt,rt,rhoskp;
long lend,rend,tend,acc,nacc;
double dacc,dnacc;
struct ValEnt *te,*re,*le;
union MultiP ls,rs,ts;
long *tsbuf;
long tsbufsize = 0;	/* set whenever tsbuf is allocated */

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	b = le->ve_rank;
	j = re->ve_rank;

	rt = re->ve_vtype & BYTES_MASK;
	lt = le->ve_vtype & BYTES_MASK;

/* ---- scalar / one-element extension for dyadic decode ----
   A scalar or one-element left argument conforms to R's LAST axis
   regardless of R's rank (this is the axis do_ddec actually contracts
   against -- see the rhoskp/tend bookkeeping just below, which keeps
   R's FIRST (rank-1) axes and folds away R's last axis). It behaves
   as if replicated to match, e.g.:
       100 decode SAMPLE   ==   100 100 100 100 decode SAMPLE
   (when SAMPLE's last axis has length 4), and a one-element vector
   left argument (e.g. (,100)) is treated identically to a bare scalar
   -- both are simply "a single value used at every contracted
   position" and must give the same result.
   A scalar or one-element RIGHT argument similarly conforms to a
   plain vector left argument, broadcasting across it and producing a
   scalar result (e.g. 10 10 10 decode 5 -> 555). This right-hand case
   is only handled here for a rank<=1 left argument, matching what's
   actually been exercised; a mismatched, non-broadcastable length on
   either side is now reported as ERR_LENGTH instead of silently
   producing garbage. */
	if (b <= 1)
	{
		long lcnt = (b == 0) ? 1L : le->ve_rho[0];

		if (lcnt == 1)
		{
			/* left is scalar / one-element: broadcast across R's last
			   axis (the axis this function actually contracts). */
			long n,tend2,ii,row,col;
			short outrank;
			double lval,base,dbound,rmax,wsum;

			n = (j >= 1) ? re->ve_rho[j-1] : 1L;

			ls.cp = DATA_AD(le);
			switch (lt) {
				case 1: lval = (double)(signed char)ls.cp[0]; break;
				case 2: lval = (double)ls.sp[0]; break;
				case 4: lval = (double)ls.lp[0]; break;
				case FLOATSIZE: lval = ls.dp[0]; break;
				default: lval = 0.0; break;
			}

			outrank = (j >= 1) ? (short)(j - 1) : (short)0;
			tend2 = 1;
			for (ii = 0; ii < outrank; ii++) tend2 *= re->ve_rho[ii];

			/* size the result. Each value is a sum of R's digits times
			   powers of lval, so max|R| times (1 + |lval| + ... +
			   |lval|^(n-1)) bounds it -- the same bound the general case
			   below uses. (Sizing by |lval|^n alone ignored R's
			   magnitude: 10⊥100 100 got a char result and printed 76.)
			   A radix that isn't a whole number means a float result. */
			rs.cp = DATA_AD(re);
			rmax = 0.0;
			for (ii = 0; ii < n * tend2; ii++)
			{
				base = fabs(elem_d(rs,rt,ii));
				if (base > rmax) rmax = base;
			}
			wsum = 0.0;
			base = 1.0;
			for (ii = 0; ii < n; ii++)
			{
				wsum += base;
				base *= fabs(lval);
			}
			dbound = rmax * wsum;

			if (dbound > 2147483647.0 || lval != floor(lval)) tt = FLOATSIZE;
			else if (dbound > 32767.0) tt = 4;
			else if (dbound > 127.0) tt = 2;
			else tt = 1;

			if ((re->ve_vtype & BYTES_MASK) > tt) tt = re->ve_vtype;
			else tt = (short)(tt | (re->ve_vtype & ~BYTES_MASK));

			te = make_temp((short)tt,outrank,tend2);
			if (!te) return(SET_ERR(ERR_WSFULL));
			re = WSVAL_PTR(*VRIGHT);	/* may have moved, re-get them */
			le = WSVAL_PTR(*VLEFT);

			for (ii = 0; ii < outrank; ii++) te->ve_rho[ii] = re->ve_rho[ii];

			rs.cp = DATA_AD(re);
			ts.cp = DATA_AD(te);
			tt = te->ve_vtype & BYTES_MASK;

			switch (tt) {
				case 1: for (ii=0;ii<tend2;ii++) ts.cp[ii] = 0; break;
				case 2: for (ii=0;ii<tend2;ii++) ts.sp[ii] = 0; break;
				case 4: for (ii=0;ii<tend2;ii++) ts.lp[ii] = 0L; break;
				case FLOATSIZE: for (ii=0;ii<tend2;ii++) ts.dp[ii] = 0.0; break;
			}
			ts.cp = DATA_AD(te);

			/* same read order as the working (non-broadcast) code
			   below: outer loop over the contracted axis, sequential
			   read across the kept axes for each contracted position;
			   only difference is L's value comes from lval every time
			   instead of real memory. */
			for (row = 0; row < n; row++)
			{
				double acc = 1.0;
				long k;
				for (k = row+1; k < n; k++) acc *= lval;

				for (col = 0; col < tend2; col++)
				{
					double rv;
					switch (rt) {
						case 1: rv = (double)(signed char)*rs.cp++; break;
						case 2: rv = (double)*rs.sp++; break;
						case 4: rv = (double)*rs.lp++; break;
						case FLOATSIZE: rv = *rs.dp++; break;
						default: rv = 0.0; break;
					}
					switch (tt) {
						case 1: ts.cp[col] += (char)(acc*rv); break;
						case 2: ts.sp[col] += (short)(acc*rv); break;
						case 4: ts.lp[col] += (long)(acc*rv); break;
						case FLOATSIZE: ts.dp[col] += acc*rv; break;
					}
				}
			}

			stack_pop(3);
			temp_stack(te);		/* our result */
			return(0);
		}
		else if (j <= 1)
		{
			/* left is a plain vector (>1 element); if R is a
			   scalar/one-element vector, broadcast it across every
			   position of L, producing a scalar result. */
			long rcnt = (j == 0) ? 1L : re->ve_rho[0];

			if (rcnt == 1)
			{
				long n,bb;
				double weight,result,rval,lv;

				n = lcnt;
				ls.cp = DATA_AD(le);
				rs.cp = DATA_AD(re);

				switch (rt) {
					case 1: rval = (double)(signed char)rs.cp[0]; break;
					case 2: rval = (double)rs.sp[0]; break;
					case 4: rval = (double)rs.lp[0]; break;
					case FLOATSIZE: rval = rs.dp[0]; break;
					default: rval = 0.0; break;
				}

				weight = 1.0;
				result = 0.0;
				for (bb = n - 1; bb >= 0; bb--)
				{
					result += weight * rval;
					switch (lt) {
						case 1: lv = (double)(signed char)ls.cp[bb]; break;
						case 2: lv = (double)ls.sp[bb]; break;
						case 4: lv = (double)ls.lp[bb]; break;
						case FLOATSIZE: lv = ls.dp[bb]; break;
						default: lv = 0.0; break;
					}
					weight *= lv;
				}

				if ((result > 2147483647.0) || (result < -2147483648.0)) tt = FLOATSIZE;
				else if ((result > 32767.0) || (result < -32768.0)) tt = 4;
				else if ((result > 127.0) || (result < -128.0)) tt = 2;
				else tt = 1;
				if (result != floor(result)) tt = FLOATSIZE;	/* e.g. 1.5 1.5⊥1 */

				if ((re->ve_vtype & BYTES_MASK) > tt) tt = re->ve_vtype;
				else tt = (short)(tt | (re->ve_vtype & ~BYTES_MASK));

				te = make_temp((short)tt,0,1L);
				if (!te) return(SET_ERR(ERR_WSFULL));
				re = WSVAL_PTR(*VRIGHT);
				le = WSVAL_PTR(*VLEFT);

				ts.cp = DATA_AD(te);
				tt = te->ve_vtype & BYTES_MASK;
				switch (tt) {
					case 1: *ts.cp = (char)result; break;
					case 2: *ts.sp = (short)result; break;
					case 4: *ts.lp = (long)result; break;
					case FLOATSIZE: *ts.dp = result; break;
				}

				stack_pop(3);
				temp_stack(te);		/* our result */
				return(0);
			}
			else if (rcnt != lcnt)
			{
				return(SET_ERR(ERR_LENGTH));
			}
			/* else rcnt == lcnt: ordinary equal-length vector decode,
			   falls through to the existing general-case code below. */
		}
		else if (j > 1 && lcnt != re->ve_rho[j-1])
		{
			/* plain vector L against a higher-rank R, but the length
			   doesn't match R's last (contracted) axis -- was silently
			   wrong before; report it cleanly instead. */
			return(SET_ERR(ERR_LENGTH));
		}
	}


	rhoskp = j - b;		/* expected target rank */

	/* calc size of left side */
	lend = 1;
	tend = 1;
	for (i=0;i<b;i++) 
	{
		lend *= le->ve_rho[i];
		if (i < rhoskp) tend *= re->ve_rho[i];
	}

	/* calc size of right side */
	rend = 1;
	for (i=0;i<j;i++) rend *= re->ve_rho[i];

	/* calc largest possible result.
	   The actual algorithm below computes, for each output position,
	   Sum_b weight_b * R[b], where weight_b = product of
	   L[b+1..lend-1] (so L[0] never appears in any weight, and a zero
	   anywhere else just zeroes out the weights whose suffix includes
	   it). A tight bound on that sum is (sum of |weight_b| over all b)
	   times (the largest |digit| actually present in R) -- bounding
	   by L's weights alone (as a previous version of this fix did)
	   ignores R's magnitude entirely and can undersize the result,
	   which then correctly -- but avoidably -- overflows below. */
	dacc = 1.0;		/* weight for b == lend-1 (empty product) */
	dnacc = 0.0;		/* running sum of |weight_b| */
	ls.cp = DATA_AD(le);
	for (i=lend-1;i>=0;i--)
	{
		dnacc += fabs(dacc);	/* dacc is weight_b for b == i here */
		if (i == 0) break;	/* L[0] itself is never a weight factor */
		dacc *= elem_d(ls,lt,i);	/* float radices used as-is, not truncated */
	}

	/* find the largest |digit| actually present in R */
	{
		double rmax,rv;

		rmax = 0.0;
		rs.cp = DATA_AD(re);
		for (i=0;i<rend;i++)
		{
			switch (rt)
			{
				case 1:
					rv = (double)(signed char)*rs.cp++;
					break;
				case 2:
					rv = (double)*rs.sp++;
					break;
				case 4:
					rv = (double)*rs.lp++;
					break;
				case FLOATSIZE:
					rv = *rs.dp++;
					break;
				default:
					rv = 0.0;
					break;
			}
			if (fabs(rv) > rmax) rmax = fabs(rv);
		}
		rs.cp = DATA_AD(re);	/* rewind: consumed above only to scan it */

		dacc = rmax * dnacc;	/* (max |digit|) * (sum of |weight_b|) */
	}

	/* figure minimum type to handle the data */
	if ((dacc > 2147483647.0) || (dacc < -2147483648.0)) tt = FLOATSIZE;
	else {
		if ((dacc > 32767.0) || (dacc < -32768.0)) tt = 4;
		else {
			if ((dacc > 127.0) || (dacc < -128.0)) tt = 2;
			else tt = 1;
		}
	}
	if (!all_whole(le)) tt = FLOATSIZE;	/* fractional radix: float result */

	/* set final temp size */
	if ((re->ve_vtype & BYTES_MASK) > tt) tt = re->ve_vtype;
	else tt = tt | (re->ve_vtype & ~BYTES_MASK);

	te = make_temp((short)tt,rhoskp,tend);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);	/* may have moved, re-get them */
	le = WSVAL_PTR(*VLEFT);

	/* NO MORE MEMORY ALLOCS TIL COMPLETION */

	/* build temp's rho */
	for (i=0;i<rhoskp;i++) te->ve_rho[i] = re->ve_rho[i];

	rs.cp = DATA_AD(re);
	ls.cp = DATA_AD(le);
	ts.cp = DATA_AD(te);

	tt = te->ve_vtype & BYTES_MASK;

	/* For char/short results, accumulate in a full-width scratch
	   buffer instead of the narrow output array directly. Each term
	   here is acc*nacc (a long computed from possibly-large weights
	   and digits); the FINAL summed value per position is what needs
	   to fit in the target type, not necessarily every intermediate
	   term along the way, and even when it doesn't, narrowing (and
	   silently wrapping) each term before adding it in is simply
	   wrong -- it can produce bogus, sign-flipped results even in
	   cases where the true sum would have fit fine. char/long/double
	   don't need this: FLOATSIZE already accumulates in double, and
	   case 4 already accumulates natively in a long (ts.lp), so only
	   cases 1 and 2 were narrowing prematurely. */
	tsbuf = (long *)0;
	if (tt == 1 || tt == 2)
	{
		tsbufsize = tend * (long)sizeof(long);
		tsbuf = (long *)sys_malloc(0, tsbufsize);
		if (!tsbuf) return(SET_ERR(ERR_WSFULL));
		for (i=0;i<tend;i++) tsbuf[i] = 0L;
	}

	/* zero the temp */
	switch (tt)
	{
		case 1:
			for (b=0;b<tend;b++) *ts.cp++ = 0;
			break;
		case 2:
			for (b=0;b<tend;b++) *ts.sp++ = 0;
			break;
		case 4:
			for (b=0;b<tend;b++) *ts.lp++ = 0L;
			break;
		case FLOATSIZE:
			for (b=0;b<tend;b++) *ts.dp++ = 0.0;
			break;
	}
	ts.cp = DATA_AD(te);

	for (b=0;b<lend;b++)
	{
		/* weight for position b: the product of the radices after it.
		   Computed in double; for an integer result the bound above
		   guarantees it fits, so it converts to acc exactly. (Multiplying
		   in long directly could overflow on a huge radix even when a
		   later zero radix makes the weight 0.) */
		dacc = 1.0;
		for (i=b+1;i<lend;i++) dacc *= elem_d(ls,lt,i);
		acc = (tt == FLOATSIZE) ? 0 : (long)dacc;

		for (j=0;j<tend;j++)	/* add n*div */
		{
			switch (rt)
			{
				case 1:
					nacc = (long)(signed char)*rs.cp++;
					break;
				case 2:
					nacc = (long)*rs.sp++;
					break;
				case 4:
					nacc = (long)*rs.lp++;
					break;
				case FLOATSIZE:
					dnacc = *rs.dp++;
					nacc = 0;	/* float R always means a float result */
					break;
				default:		/* not a valid element size */
					nacc = 0;
					break;
			}
			/* No overflow check needed here: the bound above sizes the
			   result so every term fits (or the result is float). The
			   old multiply-then-divide check could never fire; gcc
			   removed it at -O2, and at lower optimization it divided
			   by zero whenever a later radix was 0 (10 0⊥3 4). */
			switch (tt)
			{
				case 1:
				case 2:
					tsbuf[j] += acc * nacc;
					break;
				case 4:
					ts.lp[j] += acc * nacc;
					break;
				case FLOATSIZE:
					if (rt == FLOATSIZE) ts.dp[j] += dnacc * dacc;
					else ts.dp[j] += dacc * (double)nacc;
					break;
			}
		}
	}

	/* narrow the scratch accumulator into the real output now that
	   every term has been fully summed, checking the actual final
	   value against the target type's range instead of any
	   intermediate term. */
	if (tsbuf)
	{
		for (i=0;i<tend;i++)
		{
			if (tt == 1)
			{
				if (tsbuf[i] > 127L || tsbuf[i] < -128L)
				{
					sys_mfree((char *)tsbuf,tsbufsize);
					return(SET_ERR(ERR_NUMOVF));
				}
				ts.cp[i] = (char)tsbuf[i];
			}
			else
			{
				if (tsbuf[i] > 32767L || tsbuf[i] < -32768L)
				{
					sys_mfree((char *)tsbuf,tsbufsize);
					return(SET_ERR(ERR_NUMOVF));
				}
				ts.sp[i] = (short)tsbuf[i];
			}
		}
		sys_mfree((char *)tsbuf,tsbufsize);
	}

	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

short
do_drho(f)
short f;
{
short b,i,j;
long lscan,lend,rend,rscan,acc;
long outoff,inoff;
struct ValEnt *te,*re,*le;
struct MemEnt *tve;
union MultiP rs,ts;
int32_t *rho_array;
struct TakeScan *tscan;

	rho_array = LSCRATCH;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);
	b = le->ve_rank;
	if (b > 1) return(SET_ERR(ERR_RANK));

	lscan = 1;
	i = 1;
	if (b) i = (short)le->ve_rho[0];
	j = i - 1;
	rs.cp = DATA_AD(le);
	ts.cp = (char *)re->ve_rho;
	b = re->ve_rank;
	while(i--) 
	{
		switch (le->ve_vtype & BYTES_MASK) 
		{
			case 1:
				acc = (long)(signed char)*rs.cp++;
				break;
			case 2:
				acc = (long)*rs.sp++;
				break;
			case 4:
				acc = *rs.lp++;
				break;
			case FLOATSIZE:
			{
				int32_t a32;
				if (!dbl_to_i32(*rs.dp++, &a32))
					return(SET_ERR(ERR_DOMAIN));	/* V74: count beyond int32 */
				acc = a32;
				break;
			}
			default:		/* not a valid element size */
				acc = 0;
				break;
		}
		/* Dyadic rho (f==0) requires non-negative shape elements --
		   unlike take/drop (f==1/2 below), where a negative value has
		   a real, different meaning (from-the-end) and legitimately
		   flows into the shared ABS()-based sizing further down. */
		if (f == 0 && acc < 0L) return(SET_ERR(ERR_DOMAIN));
		if (f == 2) 		/* drop, convert to a take */
		{
			if (acc < 0L) 
			{
				if (i < b) acc += ts.lp[i];
				else acc = 0L;		/* drop too much */
				if (acc < 0L) acc = 0L;		/* drop too much */
			} else {
				if (i < b) acc -= ts.lp[i];
				else acc = 0L;		/* drop too much */
				if (acc > 0L) acc = 0L;		/* drop too much */
			}
		}
		rho_array[i] = acc;
		lscan *= ABS(acc);
	}
	if (f == 2) f = 1;		/* drop converted to take */

	j++;
	te = make_temp((short)re->ve_vtype,j,lscan);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);
	if (f) 		/* initialize array */
	{
		if ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE) b = ' ';
		else b = 0;
		acc = lscan * (re->ve_vtype & BYTES_MASK);
		ts.cp = DATA_AD(te);
		while (acc--) *ts.cp++ = (char)b;
	}

	i = j - 1;
	while (j--) te->ve_rho[j] = ABS(rho_array[j]);

	if (f) 	/* take */
	{
/* allocate temp data entry */
		acc = (long)(i+1) * sizeof(struct TakeScan);
		tve = MakeEnt(acc,ME_ITMP,(ws_off_t*)0);
		if (!tve) return(SET_ERR(ERR_WSFULL));
		tscan = (struct TakeScan *)((char *)tve + sizeof(struct MemEnt));
		re = WSVAL_PTR(*VRIGHT);
		te = WSVAL_PTR(*VTEMP);
		ts.cp = DATA_AD(te);
		rs.cp = DATA_AD(re);

/* NOTE:  NO MORE MEMORY ALLOCATIONS UNTIL COMPLETION */

		b = te->ve_vtype & BYTES_MASK;
		acc = 1L;		/* input weight */
		rend = 1L;		/* output weight */
		outoff = 0L;	/* initial offset */
		inoff = 0L;		/* initial offset */
		for (j=0;j<=i;j++) 
		{
			rscan = 1L;
			if (re->ve_rank > j) rscan = re->ve_rho[j];
			lend = rho_array[j];
			if (lend < 0L) {
				tscan[j].istart = rscan + lend;
				tscan[j].ostart = 0L - tscan[j].istart;
				if (tscan[j].istart < 0L) tscan[j].istart = 0L;
				if (tscan[j].ostart < 0L) tscan[j].ostart = 0L;
			} else tscan[j].istart = tscan[j].ostart = 0L;

			tscan[j].oscan = te->ve_rho[j] - tscan[j].ostart;
			tscan[j].iscan = rscan - tscan[j].istart;
			outoff += tscan[j].ostart * rend;
			inoff += tscan[j].istart * acc;
			tscan[j].ostart = tscan[j].oscan;
			tscan[j].istart = tscan[j].iscan;

			lscan = rscan - ABS(lend);		/* skip factor */
			if (lscan < 0L) {		/* an output skip? */
				tscan[j].oskip = rend * (0L - lscan);
				tscan[j].iskip = 0L;
			} else {				/* no, an input skip */
				tscan[j].iskip = acc * lscan;
				tscan[j].oskip = 0L;
			}

			rend *= te->ve_rho[j];		/* compute output weight */
			acc *= rscan;	/* compute input weight */

		}

		switch (b) 	/* skip to starts */
		{
			case 1:
				rs.cp += inoff;
				ts.cp += outoff;
				break;
			case 2:
				rs.sp += inoff;
				ts.sp += outoff;
				break;
			case 4:
				rs.lp += inoff;
				ts.lp += outoff;
				break;
			case FLOATSIZE:
				rs.dp += inoff;
				ts.dp += outoff;
				break;
		}

		do {		/* main take loop */
			while (tscan[0].oscan-- && tscan[0].iscan--) 
			{
				switch (b) 
				{
					case 1:
						*ts.cp++ = *rs.cp++;
						break;
					case 2:
						*ts.sp++ = *rs.sp++;
						break;
					case 4:
						*ts.lp++ = *rs.lp++;
						break;
					case FLOATSIZE:
						*ts.dp++ = *rs.dp++;
						break;
				}
			}
			j = 0;
			do {
				switch (b) 
				{
					case 1:
						ts.cp += tscan[j].oskip;
						rs.cp += tscan[j].iskip;
						break;
					case 2:
						ts.sp += tscan[j].oskip;
						rs.sp += tscan[j].iskip;
						break;
					case 4:
						ts.lp += tscan[j].oskip;
						rs.lp += tscan[j].iskip;
						break;
					case FLOATSIZE:
						ts.dp += tscan[j].oskip;
						rs.dp += tscan[j].iskip;
						break;
				}
				tscan[j].oscan = tscan[j].ostart;
				tscan[j].iscan = tscan[j].istart;
				j++;
				if (j <= i) {
					tscan[j].oscan--;
					tscan[j].iscan--;
				}
			} while (j <= i && (tscan[j].oscan <= 0 || tscan[j].iscan <= 0));
		} while (j <= i);
		tve->me_reference = (ws_off_t)0;		/* delete */
	} else {	/* standard rho */
		ts.cp = DATA_AD(te);
		rs.cp = DATA_AD(re);

		rscan = re->ve_length;

		i = te->ve_vtype & BYTES_MASK;
		while (lscan--) {
			switch(i) {
				case 1:
					*ts.cp++ = *rs.cp++;
					break;
				case 2:
					*ts.sp++ = *rs.sp++;
					break;
				case 4:
					*ts.lp++ = *rs.lp++;
					break;
				case FLOATSIZE:
					*ts.dp++ = *rs.dp++;
					break;
			}
			rscan--;
			if (!rscan) 
			{
				rs.cp = DATA_AD(re);
				rscan = re->ve_length;
			}
		}
	}

/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

/* now converted into the new replicate + the old expand */
short
do_compx(f)
short f;
{
short b,lrank,rrank,lbyts,byts;
struct ValEnt *le,*re,*te;
long lscan,lsc,tscan,oscan,nscan,rlen,nzcount,sv;
union MultiP ts,rs,ls;
int ii;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);
	lrank = (short)le->ve_rank;	
	if (lrank > 1 || lrank < 0) return(SET_ERR(ERR_RANK));
	if (le->ve_rank) lscan = le->ve_rho[0];
	else lscan = 1L;
	lbyts = le->ve_vtype & BYTES_MASK;

	if ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE) b = ' ';
	else b = 0;

	if (!f) 		/* if compress */
	{
		/* rlen = length of one right-arg row (last axis), same
		 * convention as the expand branch's rlen just below. A
		 * scalar left arg (lscan==1) broadcasts across every
		 * position in the row instead of requiring lscan==rlen. */
		rlen = re->ve_rank ? re->ve_rho[0] : 1L;
		if (lscan != 1L && lscan != rlen) return(SET_ERR(ERR_LENGTH));

		oscan = 0L;
		ts.cp = (char *)DATA_AD(le);
		if (lscan == 1L)
		{
			/* scalar left: read the single value once, then
			 * count as if it applied at every one of rlen
			 * positions -- avoids re-reading le->ve_rho[0]
			 * below, which isn't valid for a rank-0 value. */
			sv = 0L;
			switch (lbyts)
			{
				case 1:
					sv = abs((int)(signed char)*ts.cp);
					break;
				case 2:
					sv = abs((int)*ts.sp);
					break;
				case 4:
					sv = abs((int)*ts.lp);
					break;
				case FLOATSIZE:
				{
					/* V74: abs() like the integer cases -- without
					   it a negative float count (fill elements)
					   sized the result negative -- and a safe
					   conversion */
					int32_t c32;
					if (!dbl_to_i32(*ts.dp, &c32))
						return(SET_ERR(ERR_DOMAIN));
					sv = labs((long)c32);
					break;
				}
			}
			oscan = sv * rlen;
		} else {
			while (lscan--) 
			{
				switch (lbyts) 
				{
					case 1:
						oscan += abs((int)(signed char)*ts.cp++);
						break;
					case 2:
						oscan += abs((int)*ts.sp++);
						break;
					case 4:
						oscan += abs((int)*ts.lp++);
						break;
					case FLOATSIZE:
					{
						/* V74: abs() was missing here too, so
						   1 ¯2.0 1/'abc' sized the result at 0 and
						   the copy loop below wrote 4 elements past
						   it into the workspace */
						int32_t c32;
						if (!dbl_to_i32(*ts.dp++, &c32))
							return(SET_ERR(ERR_DOMAIN));
						oscan += labs((long)c32);
						break;
					}
				}
			}
		}
		/* lscan drives the main loop's row-reset stride below;
		 * le->ve_rho[0] is only valid when le actually has rank
		 * (guard matches the original assignment at the top of
		 * this function, e.g. lscan==1 stays 1 for a scalar). */
		lscan = le->ve_rank ? le->ve_rho[0] : 1L;
	} else {		/* an expand */
		/* count of nonzero left-vector entries must equal the right
		 * arg's actual last-axis size -- otherwise the block below
		 * either reads past the right arg's data (too many nonzero
		 * entries) or silently drops trailing elements (too few).
		 * Same class of bug found and fixed in do_expand1() (first
		 * axis); confirmed to reproduce here too. */
		rlen = re->ve_rank ? re->ve_rho[0] : 1L;
		nzcount = 0L;
		ts.cp = (char *)DATA_AD(le);
		tscan = lscan;
		while (tscan--) 
		{
			switch (lbyts) 
			{
				case 1:
					if (*ts.cp++) nzcount++;
					break;
				case 2:
					if (*ts.sp++) nzcount++;
					break;
				case 4:
					if (*ts.lp++) nzcount++;
					break;
				case FLOATSIZE:
					if (*ts.dp++ != 0.0) nzcount++;
					break;
			}
		}
		if (nzcount != rlen) return(SET_ERR(ERR_LENGTH));
		oscan = lscan;	/* an expand */
	}
	tscan = re->ve_length;
	nscan = oscan;
	rrank = (short)re->ve_rank;
	for (ii=1;ii<rrank;ii++) if (ii) nscan *= re->ve_rho[ii];
	if (!rrank) rrank = 1;
	if (f) tscan = nscan;

	te = make_temp((short)re->ve_vtype,rrank,nscan);
	if (!te) return(SET_ERR(ERR_WSFULL));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	te->ve_vtype = re->ve_vtype;
	te->ve_rank = rrank;
	te->ve_rho[0] = oscan;
	for (ii=1;ii<rrank;ii++) te->ve_rho[ii] = re->ve_rho[ii];
	
	ts.cp = DATA_AD(te);
	ls.cp = DATA_AD(le);
	rs.cp = DATA_AD(re);
	byts = re->ve_vtype & BYTES_MASK;
	lsc = lscan;
	while (tscan--) 
	{
		switch (lbyts) 
		{
			case 1:
				ii = (int)(signed char)*ls.cp++;
				break;
			case 2:
				ii = (int)*ls.sp++;
				break;
			case 4:
				ii = (int)*ls.lp++;
				break;
			case FLOATSIZE:
				if (f) {
					/* expand: only zero/nonzero matters -- same
					   test as the nzcount pass above (V74; 0.5
					   used to count as nonzero there, zero here) */
					ii = (*ls.dp++ != 0.0);
				} else {
					/* replicate count; already range-checked by
					   the sizing pass above, so this can't fail */
					int32_t c32;
					if (!dbl_to_i32(*ls.dp++, &c32))
						return(SET_ERR(ERR_DOMAIN));	/* V74 */
					ii = (int)c32;
				}
				break;
		}
		if (!--lsc) 
		{
			lsc = lscan;
			ls.cp = DATA_AD(le);
		}
		switch (byts) 
		{
			case 1:
				if (!f)	/* replicate (compress) */
				{
					if (ii > 0) 
					{
						while (ii--) *ts.cp++ = *rs.cp;
					} else {
						if (ii < 0)
						{
							ii = abs(ii);
							while (ii--) *ts.cp++ = (char)b;
						}
					}
					rs.cp++;
				} else {
					if (ii) *ts.cp++ = *rs.cp;
					if (f && !ii) *ts.cp++ = (char)b;
					else rs.cp++;
				}
				break;
			case 2:
				if (!f)	/* replicate (compress) */
				{
					if (ii > 0) 
					{
						while (ii--) *ts.sp++ = *rs.sp;
					} else {
						if (ii < 0)
						{
							ii = abs(ii);
							while (ii--) *ts.sp++ = 0;
						}
					}
					rs.sp++;
				} else {
					if (ii) *ts.sp++ = *rs.sp;
					if (f && !ii) *ts.sp++ = 0;
					else rs.sp++;
				}
				break;
			case 4:
				if (!f)	/* replicate (compress) */
				{
					if (ii > 0) 
					{
						while (ii--) *ts.lp++ = *rs.lp;
					} else {
						if (ii < 0)
						{
							ii = abs(ii);
							while (ii--) *ts.lp++ = 0L;
						}
					}
					rs.lp++;
				} else {
					if (ii) *ts.lp++ = *rs.lp;
					if (f && !ii) *ts.lp++ = 0L;
					else rs.lp++;
				}
				break;
			case FLOATSIZE:
				if (!f)	/* replicate (compress) */
				{
					if (ii > 0) 
					{
						while (ii--) *ts.dp++ = *rs.dp;
					} else {
						if (ii < 0)
						{
							ii = abs(ii);
							while (ii--) *ts.dp++ = 0.0;
						}
					}
					rs.dp++;
				} else {
					if (ii) *ts.dp++ = *rs.dp;
					if (f && !ii) *ts.dp++ = 0.0;
					else rs.dp++;
				}
				break;
		}
	}

/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

short
do_diota()
{
long lscan,rscan,lend;
struct ValEnt *re,*le,*te;
union MultiP rp,lp,tp;
short b;
REG short j,k;
long acc,l;
double dl = 0.0, dacc = 0.0;	/* the values compared when usefloat */
short usefloat;

	VTEMP = (ws_off_t*)0;
	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	lscan = le->ve_length;
	rscan = re->ve_length;
	
	j = 1;
	if (lscan > 127) j = 2;
	if (lscan > 32767) j = 4;

	if ((re->ve_vtype & VARIABLE_BIT) && j <= (re->ve_vtype & BYTES_MASK)) te = re;
	else {
		b = (short)re->ve_rank;
		te = make_temp((short)(j | CONST_CODE),b,rscan);
		if (!te) return(SET_ERR(ERR_WSFULL));
		re = WSVAL_PTR(*VRIGHT);
		le = WSVAL_PTR(*VLEFT);
		for (j=0;j<b;j++) te->ve_rho[j] = re->ve_rho[j];
	}

	j = re->ve_vtype & BYTES_MASK;
	k = le->ve_vtype & BYTES_MASK;
	b = te->ve_vtype & BYTES_MASK;
	/* V75: same rule as do_depsilon() (L∊R): if either side is float,
	   compare both as doubles, exactly, so 1 2 3⍳2.5 doesn't find the
	   2 and 0.1 0.2 0.3⍳0.3 finds the 3 -- both used to be truncated to
	   integers first, which matched wrong elements (and, before V74,
	   converted out-of-range floats with undefined behavior). A float
	   holding a whole number still matches the integer. Otherwise
	   integers (and characters, by byte value) compare as before. No
	   comparison tolerance yet (no ⎕CT -- see apl_backend_ideas_v1.md),
	   the same as = and ∊. */
	usefloat = (j == FLOATSIZE || k == FLOATSIZE) ? 1 : 0;
	tp.cp = DATA_AD(te);
	rp.cp = DATA_AD(re);
	while (rscan) {
		switch (j) {
			case 1:
				l = (long)(signed char)*rp.cp++;
				break;
			case 2:
				l = (long)*rp.sp++;
				break;
			case 4:
				l = *rp.lp++;
				break;
			case FLOATSIZE:
				l = 0;
				dl = *rp.dp++;
				break;
			default:		/* not a valid element size */
				l = 0;
				break;
		}
		if (usefloat && j != FLOATSIZE) dl = (double)l;
		lp.cp = DATA_AD(le);
		lend = 0L;
		while (lend < lscan) {
			switch (k) {
				case 1:
					acc = (long)(signed char)*lp.cp++;
					break;
				case 2:
					acc = (long)*lp.sp++;
					break;
				case 4:
					acc = *lp.lp++;
					break;
				case FLOATSIZE:
					acc = 0;
					dacc = *lp.dp++;
					break;
				default:		/* not a valid element size */
					acc = 0;
					break;
			}
			if (usefloat) {
				if (k != FLOATSIZE) dacc = (double)acc;
				if (dacc == dl) break;
			} else if (acc == l) break;
			lend++;
		}
		lend += wspace->origin;
		switch (b) {
			case 1:
				*tp.cp++ = (char)lend;		
				break;
			case 2:
				*tp.sp++ = (short)lend;		
				break;
			case 4:
				*tp.lp++ = lend;		
				break;
			case FLOATSIZE:
				*tp.dp++ = (double)lend;
				break;
		}
		rscan--;		
	}

/* make sure it's not a char type */
	te->ve_vtype = (te->ve_vtype & BYTES_MASK) | CONST_CODE;

/* pop 'em all, including any temp stack val */
	if (VTEMP) stack_pop(3);
	else stack_pop(2);

	temp_stack(te);		/* stack result */
	return(0);
}

/* Membership, L∊R (dyadic epsilon).
 *
 * Unlike the scalar ops in the generic do_dyop() dispatch below, this is
 * NOT scalar-extended/conforming: R can be any shape, and the result
 * always takes on the shape of L, not some conformed shape of L and R
 * together. Each element of L is tested for equality against every
 * element in the ravel of R; result is a byte (boolean 0/1) array.
 *
 * Modeled on do_diota() above (same "scan one side for each element of
 * the other" structure), but with the loop nesting and output flipped:
 * do_diota() shapes its result off R and scans L for each R element;
 * this shapes off L and scans R for each L element, writing a 0/1 hit
 * instead of a found index.
 *
 * Deliberately does NOT reuse do_diota()'s truncate-to-long comparison
 * (that drops fractional precision on float compares, e.g. would wrongly
 * treat 3.5 and 3 as equal). Instead this promotes both sides to double
 * for the compare whenever either operand's element type is FLOATSIZE,
 * matching the dacc/csize promotion pattern used in the generic scalar
 * loop further down in do_dyop().
 *
 * Char data (QUOT_CODE) is stored as bytes and falls through the case 1
 * byte compare same as do_diota() does -- no special-casing here either,
 * for consistency with that existing simplification.
 */
short
do_depsilon()
{
struct ValEnt *re,*le,*te;
union MultiP rp,lp,tp;
short i,j,k;
long lscan,rscan,rlen;
long acc = 0,acc2 = 0,found;
double dacc,dacc2;
short usefloat;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE ||
	    (le->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));

	lscan = le->ve_length;
	rlen = re->ve_length;

	te = make_temp((short)BCONST_CODE,(short)le->ve_rank,lscan);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);	/* may have moved, re-get them */
	le = WSVAL_PTR(*VLEFT);

	for (k=0;k<le->ve_rank;k++) te->ve_rho[k] = le->ve_rho[k];

	i = le->ve_vtype & BYTES_MASK;
	j = re->ve_vtype & BYTES_MASK;
	usefloat = (i == FLOATSIZE || j == FLOATSIZE) ? 1 : 0;

	lp.cp = DATA_AD(le);
	tp.cp = DATA_AD(te);

	while (lscan) {
		dacc = 0.0;
		switch (i) {
			case 1:
				acc = (long)(signed char)*lp.cp++;
				break;
			case 2:
				acc = (long)*lp.sp++;
				break;
			case 4:
				acc = *lp.lp++;
				break;
			case FLOATSIZE:
				dacc = *lp.dp++;
				break;
		}
		if (usefloat && i != FLOATSIZE) dacc = (double)acc;

		found = 0L;
		rp.cp = DATA_AD(re);
		rscan = rlen;
		while (rscan) {
			dacc2 = 0.0;
			switch (j) {
				case 1:
					acc2 = (long)(signed char)*rp.cp++;
					break;
				case 2:
					acc2 = (long)*rp.sp++;
					break;
				case 4:
					acc2 = *rp.lp++;
					break;
				case FLOATSIZE:
					dacc2 = *rp.dp++;
					break;
			}
			if (usefloat) {
				if (j != FLOATSIZE) dacc2 = (double)acc2;
				if (dacc == dacc2) { found = 1L; break; }
			} else
			{
				if (acc == acc2) { found = 1L; break; }
			}
			rscan--;
		}

		*tp.cp++ = (char)found;
		lscan--;
	}

	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

/* Squad, L squad R (dyadic index, overstrike [+]).
 *
 * L is a list of N coordinates into R. Normally rank 2, shape N x rank(R):
 * one full coordinate per row, values in ordinary axis order (first axis
 * first, matching how the coordinates would be typed by hand). As a
 * convenience when R is itself a vector (rank 1) -- the common case, and
 * matching Dyalog's dyadic squad -- L may also be given as a plain rank-1
 * vector of N values, each one being the single coordinate directly
 * (equivalent to an N x 1 L). Any other L shape is ERR_RANK, as is
 * indexing into a scalar R (no coordinate is valid there).
 *
 * Result is always a rank-1 vector of N elements, same vtype as R -- this
 * is a pure gather, each output element copied whole from wherever its
 * coordinate points in R. Unlike the generic scalar ops in do_dyop()'s
 * fallthrough loop further down, R is not scalar-extended/conformed
 * against L -- R only supplies the space being indexed into, not an
 * operand being combined elementwise -- so this is structured like
 * do_depsilon()/do_dtrans() above rather than that loop.
 *
 * Coordinate values honor the workspace's index origin the same way
 * do_dtrans()'s axis vector does, and float coordinates truncate the same way (no rounding).
 * Out-of-range coordinates are ERR_INDEX.
 *
 * Axis-size lookup uses this codebase's reversed rho convention (ve_rho[0]
 * = last/innermost axis size, ve_rho[rank-1] = first/outermost axis size --
 * see do_compx1()'s comment above), walking each row's coordinates in
 * ordinary first-axis-first order and folding them into a ravel offset
 * like a mixed-radix number, most significant (first/outermost) axis
 * first -- offset = offset*axis_size + coord, repeated per axis.
 *
 * L is read as one flat sequence regardless of which of the two accepted
 * shapes it came in as: both lay out N groups of rank(R) values (or N
 * groups of 1, for the vector-L convenience form) contiguously in the
 * same row-major order this loop reads them in, so no shape-specific
 * branch is needed past the initial validation.
 *
 * No monadic sense -- see OP_SQUAD in parse.h; monadic squad is a
 * separate future expansion of monadic iota, not part of this op.
 */
short
do_squad()
{
struct ValEnt *re,*le,*te;
union MultiP rp,lp,tp;
short rrank,lk,rk;
long i,a,N,ncols,idx,offset,axsize,origin;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	if ((re->ve_vtype | VARIABLE_BIT) == STRUCT_CODE ||
	    (le->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) return(SET_ERR(ERR_DOMAIN));

	rrank = re->ve_rank;
	if (!rrank) return(SET_ERR(ERR_RANK));		/* can't index a scalar */

	if (le->ve_rank == 2) {
		ncols = le->ve_rho[0];
		N = le->ve_rho[1];
		if (ncols != rrank) return(SET_ERR(ERR_RANK));
	} else if (le->ve_rank == 1 && rrank == 1) {
		ncols = 1;		/* each L element IS the coordinate */
		N = le->ve_length;
	} else {
		return(SET_ERR(ERR_RANK));
	}

	te = make_temp((short)re->ve_vtype,1,N);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);	/* may have moved */
	le = WSVAL_PTR(*VLEFT);

	te->ve_rho[0] = N;

	origin = wspace->origin;
	lk = le->ve_vtype & BYTES_MASK;
	rk = re->ve_vtype & BYTES_MASK;
	lp.cp = DATA_AD(le);
	tp.cp = DATA_AD(te);

	for (i = 0L; i < N; i++) {
		offset = 0L;
		for (a = 0L; a < ncols; a++) {
			switch (lk) {
				case 1:
					idx = (long)(signed char)*lp.cp++;
					break;
				case 2:
					idx = (long)*lp.sp++;
					break;
				case 4:
					idx = *lp.lp++;
					break;
				case FLOATSIZE:
				{
					int32_t i32;
					if (!dbl_to_i32(*lp.dp++, &i32))
						return(SET_ERR(ERR_INDEX));	/* V74 */
					idx = i32;
					break;
				}
				default:		/* not a valid element size */
					idx = 0;
					break;
			}
			idx -= origin;
			axsize = re->ve_rho[rrank - 1 - a];
			if (idx < 0L || idx >= axsize) return(SET_ERR(ERR_INDEX));
			offset = offset * axsize + idx;
		}

		rp.cp = DATA_AD(re);
		switch (rk) {
			case 1:
				rp.cp += offset;
				*tp.cp++ = *rp.cp;
				break;
			case 2:
				rp.sp += offset;
				*tp.sp++ = *rp.sp;
				break;
			case 4:
				rp.lp += offset;
				*tp.lp++ = *rp.lp;
				break;
			case FLOATSIZE:
				rp.dp += offset;
				*tp.dp++ = *rp.dp;
				break;
		}
	}

	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

/* Compress over the FIRST (outermost) axis -- overstrike-entered "compress
 * first" variant (traditionally shown as slash overstruck with a
 * horizontal bar, entered here via / overstruck with -).
 *
 * do_compx() above handles plain / and \, which operate on the LAST
 * (innermost, fastest-varying) axis: elements are contiguous in memory,
 * so it can cycle through the left vector one scalar at a time.
 *
 * The first axis is the outermost/slowest-varying one, so the unit being
 * kept, dropped, or filled is a whole contiguous BLOCK of
 * (product of all other axis sizes) elements, not a single scalar.
 * Confirmed against this codebase's rho-index convention (ve_rho[0] =
 * last-axis size, ve_rho[rank-1] = first-axis size) via do_dtrans() and
 * do_drho().
 *
 * Left arg must be a rank<=1 integer vector whose length equals the
 * right arg's first-axis size (ve_rho[rank-1]), OR a scalar (or
 * one-element vector), which broadcasts the same value to every row --
 * mirrors do_compx()'s own scalar-broadcast fix for the last axis.
 */
short
do_compx1()
{
short lrank,rrank,lbyts,byts,b;
struct ValEnt *le,*re,*te;
long lscan,lsc,blksize,oscan,nscan,rlen,row,ii,copies,z,sv;
union MultiP ts,rs,ls;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);
	lrank = (short)le->ve_rank;
	if (lrank > 1 || lrank < 0) return(SET_ERR(ERR_RANK));
	if (le->ve_rank) lscan = le->ve_rho[0];
	else lscan = 1L;
	lbyts = le->ve_vtype & BYTES_MASK;

	if ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE) b = ' ';
	else b = 0;

	rrank = (short)re->ve_rank;
	rlen = re->ve_rank ? re->ve_rho[re->ve_rank - 1] : 1L;
	if (!rrank) rrank = 1;
	if (lscan != 1L && lscan != rlen) return(SET_ERR(ERR_LENGTH));

	/* elements in one "row" -- all axes except the first */
	blksize = 1L;
	for (ii = 0; ii < rrank - 1; ii++) blksize *= re->ve_rho[ii];

	/* result length along the first axis = sum of |left vector|,
	 * or (scalar magnitude * rlen) when left is a scalar/one-element
	 * vector broadcasting across every row -- mirrors do_compx()'s
	 * scalar-broadcast fix. */
	oscan = 0L;
	ls.cp = (char *)DATA_AD(le);
	if (lscan == 1L)
	{
		sv = 0L;
		switch (lbyts)
		{
			case 1:
				sv = abs((int)(signed char)*ls.cp);
				break;
			case 2:
				sv = abs((int)*ls.sp);
				break;
			case 4:
				sv = abs((int)*ls.lp);
				break;
			case FLOATSIZE:
			{
				/* V74: abs() like the integer cases (a negative
				   float count sized the result negative) and a
				   safe conversion */
				int32_t c32;
				if (!dbl_to_i32(*ls.dp, &c32))
					return(SET_ERR(ERR_DOMAIN));
				sv = labs((long)c32);
				break;
			}
		}
		oscan = sv * rlen;
	} else {
		row = lscan;
		while (row--) 
		{
			switch (lbyts) 
			{
				case 1:
					oscan += abs((int)(signed char)*ls.cp++);
					break;
				case 2:
					oscan += abs((int)*ls.sp++);
					break;
				case 4:
					oscan += abs((int)*ls.lp++);
					break;
				case FLOATSIZE:
				{
					/* V74: abs() was missing -- a negative float
					   count undersized the result and the copy
					   loop overran it */
					int32_t c32;
					if (!dbl_to_i32(*ls.dp++, &c32))
						return(SET_ERR(ERR_DOMAIN));
					oscan += labs((long)c32);
					break;
				}
			}
		}
	}

	nscan = oscan * blksize;

	te = make_temp((short)re->ve_vtype,rrank,nscan);
	if (!te) return(SET_ERR(ERR_WSFULL));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	te->ve_vtype = re->ve_vtype;
	te->ve_rank = rrank;
	for (ii = 0; ii < rrank - 1; ii++) te->ve_rho[ii] = re->ve_rho[ii];
	te->ve_rho[rrank - 1] = oscan;

	ts.cp = DATA_AD(te);
	ls.cp = DATA_AD(le);
	rs.cp = DATA_AD(re);
	byts = re->ve_vtype & BYTES_MASK;

	/* lsc = number of right-arg rows to walk. For a vector left arg
	 * this is just lscan (one left element per row, as before). For
	 * a scalar/one-element left arg (lscan==1) it's rlen -- every
	 * row of the right arg gets the same broadcast count, mirroring
	 * do_compx()'s scalar-broadcast fix. */
	lsc = (lscan == 1L) ? rlen : lscan;
	if (lscan == 1L)
	{
		/* scalar: read the single left value once up front; each
		 * row below reuses it without re-reading or advancing
		 * ls.cp (there's only one element to read). */
		switch (lbyts) 
		{
			case 1:
				ii = (long)(signed char)*ls.cp;
				break;
			case 2:
				ii = (long)*ls.sp;
				break;
			case 4:
				ii = *ls.lp;
				break;
			case FLOATSIZE:
			{
				int32_t c32;
				if (!dbl_to_i32(*ls.dp, &c32))
					return(SET_ERR(ERR_DOMAIN));	/* V74 */
				ii = c32;
				break;
			}
		}
	}
	for (row = 0; row < lsc; row++) 
	{
		if (lscan != 1L)
		{
			switch (lbyts) 
			{
				case 1:
					ii = (long)(signed char)*ls.cp++;
					break;
				case 2:
					ii = (long)*ls.sp++;
					break;
				case 4:
					ii = *ls.lp++;
					break;
				case FLOATSIZE:
				{
					int32_t c32;
					if (!dbl_to_i32(*ls.dp++, &c32))
						return(SET_ERR(ERR_DOMAIN));	/* V74 */
					ii = c32;
					break;
				}
			}
		}
		if (ii > 0) 
		{
			copies = ii;
			while (copies--) 
			{
				switch (byts) 
				{
					case 1:
						memcpy(ts.cp,rs.cp,(size_t)blksize);
						ts.cp += blksize;
						break;
					case 2:
						memcpy(ts.sp,rs.sp,(size_t)(blksize * 2));
						ts.sp += blksize;
						break;
					case 4:
						memcpy(ts.lp,rs.lp,(size_t)(blksize * 4));
						ts.lp += blksize;
						break;
					case FLOATSIZE:
						memcpy(ts.dp,rs.dp,(size_t)(blksize * FLOATSIZE));
						ts.dp += blksize;
						break;
				}
			}
		} else if (ii < 0) {
			copies = -ii;
			while (copies--) 
			{
				switch (byts) 
				{
					case 1:
						memset(ts.cp,b,(size_t)blksize);
						ts.cp += blksize;
						break;
					case 2:
						for (z=0;z<blksize;z++) *ts.sp++ = 0;
						break;
					case 4:
						for (z=0;z<blksize;z++) *ts.lp++ = 0L;
						break;
					case FLOATSIZE:
						for (z=0;z<blksize;z++) *ts.dp++ = 0.0;
						break;
				}
			}
		}
		/* one right-arg row consumed regardless of ii's sign/value,
		 * same convention as do_compx()'s unconditional rs.cp++ */
		switch (byts) 
		{
			case 1:
				rs.cp += blksize;
				break;
			case 2:
				rs.sp += blksize;
				break;
			case 4:
				rs.lp += blksize;
				break;
			case FLOATSIZE:
				rs.dp += blksize;
				break;
		}
	}

/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

/* Expand over the FIRST (outermost) axis -- overstrike-entered "expand
 * first" variant (traditionally shown as backslash overstruck with a
 * horizontal bar, entered here via \ overstruck with -).
 *
 * Mirrors do_compx1() structurally (block-per-first-axis-position instead
 * of do_compx()'s per-scalar cycling), but follows do_compx()'s f=1
 * (expand) branch semantics exactly:
 *   - result first-axis size = the left vector's own length (no summing
 *     of magnitudes, unlike compress).
 *   - each left element is treated as a plain zero/nonzero test (not a
 *     replicate count): nonzero copies one block from the right arg and
 *     consumes it; zero inserts a filler block and does NOT consume a
 *     right-arg block.
 *
 * Unlike do_compx()'s own expand branch (plain \), this DOES check the
 * count of nonzero left-vector entries against the right arg's actual
 * first-axis size and raises ERR_LENGTH on mismatch. Confirmed by testing
 * that without this check, a too-long left vector reads garbage past the
 * end of the right arg's data, and a too-short one silently drops the
 * right arg's trailing rows with no error -- both worse than a LENGTH
 * error. Plain \ (do_compx(), f=1) still has this gap; not touched here.
 */
short
do_expand1()
{
short lrank,rrank,lbyts,byts,b;
struct ValEnt *le,*re,*te;
long lscan,blksize,oscan,nscan,rlen,nzcount,row,ii,z;
union MultiP ts,rs,ls;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);
	lrank = (short)le->ve_rank;
	if (lrank > 1 || lrank < 0) return(SET_ERR(ERR_RANK));
	if (le->ve_rank) lscan = le->ve_rho[0];
	else lscan = 1L;
	lbyts = le->ve_vtype & BYTES_MASK;

	if ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE) b = ' ';
	else b = 0;

	rrank = (short)re->ve_rank;
	rlen = re->ve_rank ? re->ve_rho[re->ve_rank - 1] : 1L;
	if (!rrank) rrank = 1;

	/* the count of nonzero entries in the left vector must equal the
	 * right arg's actual first-axis size -- otherwise some right-arg
	 * rows would go unconsumed (silently dropped) or the block copy
	 * below would run past the end of the right arg's data (garbage
	 * read). Both failure modes were observed before this check was
	 * added. */
	nzcount = 0L;
	ls.cp = (char *)DATA_AD(le);
	row = lscan;
	while (row--) 
	{
		switch (lbyts) 
		{
			case 1:
				if (*ls.cp++) nzcount++;
				break;
			case 2:
				if (*ls.sp++) nzcount++;
				break;
			case 4:
				if (*ls.lp++) nzcount++;
				break;
			case FLOATSIZE:
				if (*ls.dp++ != 0.0) nzcount++;
				break;
		}
	}
	if (nzcount != rlen) return(SET_ERR(ERR_LENGTH));

	oscan = lscan;		/* expand: result first-axis size = left vector length */

	/* elements in one "row" -- all axes except the first */
	blksize = 1L;
	for (ii = 0; ii < rrank - 1; ii++) blksize *= re->ve_rho[ii];

	nscan = oscan * blksize;

	te = make_temp((short)re->ve_vtype,rrank,nscan);
	if (!te) return(SET_ERR(ERR_WSFULL));
	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	te->ve_vtype = re->ve_vtype;
	te->ve_rank = rrank;
	for (ii = 0; ii < rrank - 1; ii++) te->ve_rho[ii] = re->ve_rho[ii];
	te->ve_rho[rrank - 1] = oscan;

	ts.cp = DATA_AD(te);
	ls.cp = DATA_AD(le);
	rs.cp = DATA_AD(re);
	byts = re->ve_vtype & BYTES_MASK;

	for (row = 0; row < lscan; row++) 
	{
		switch (lbyts) 
		{
			case 1:
				ii = (long)(signed char)*ls.cp++;
				break;
			case 2:
				ii = (long)*ls.sp++;
				break;
			case 4:
				ii = *ls.lp++;
				break;
			case FLOATSIZE:
				/* V74: only zero/nonzero matters here, so test it
				   directly, the same test the nzcount pass above
				   uses -- no conversion to overflow, and 0.5 no
				   longer counts as nonzero there but zero here */
				ii = (*ls.dp++ != 0.0);
				break;
		}
		if (ii) 
		{
			/* nonzero: copy one block from the right arg, consuming it */
			switch (byts) 
			{
				case 1:
					memcpy(ts.cp,rs.cp,(size_t)blksize);
					ts.cp += blksize;
					rs.cp += blksize;
					break;
				case 2:
					memcpy(ts.sp,rs.sp,(size_t)(blksize * 2));
					ts.sp += blksize;
					rs.sp += blksize;
					break;
				case 4:
					memcpy(ts.lp,rs.lp,(size_t)(blksize * 4));
					ts.lp += blksize;
					rs.lp += blksize;
					break;
				case FLOATSIZE:
					memcpy(ts.dp,rs.dp,(size_t)(blksize * FLOATSIZE));
					ts.dp += blksize;
					rs.dp += blksize;
					break;
			}
		} else {
			/* zero: insert a filler block, right arg NOT consumed */
			switch (byts) 
			{
				case 1:
					memset(ts.cp,b,(size_t)blksize);
					ts.cp += blksize;
					break;
				case 2:
					for (z=0;z<blksize;z++) *ts.sp++ = 0;
					break;
				case 4:
					for (z=0;z<blksize;z++) *ts.lp++ = 0L;
					break;
				case FLOATSIZE:
					for (z=0;z<blksize;z++) *ts.dp++ = 0.0;
					break;
			}
		}
	}

/* pop em all, including temp */
	stack_pop(3);
	temp_stack(te);		/* our result */
	return(0);
}

/* Dyadic ?, "deal": L?R produces a length-L vector containing L
 * distinct random integers, in random order, drawn from
 * origin..origin+R-1 (i.e. a random L-subset of ⍳R). L and R must
 * each be a scalar or a one-element vector (deal_arg(), below): an
 * empty or longer vector is ERR_LENGTH -- the old code read the first
 * element regardless, past the end of an empty one. A float argument
 * is range-checked BEFORE conversion (anything outside 0..INTEGER_MAX,
 * or NaN, is ERR_DOMAIN -- converting first was undefined behavior for
 * e.g. 5?1E10), then truncated as before, same as monadic ⍳.
 *
 * Algorithm: partial Fisher-Yates (selection sampling) over
 * [0,1,...,R-1] rather than generate-and-reject -- for i=0..L-1,
 * swap in a uniformly random element from the remaining i..R-1 and
 * take it. Zero retries, O(L) swaps, regardless of how close L is
 * to R (generate-and-reject degrades badly as L approaches R; see
 * session notes).
 *
 * Unlike a textbook Fisher-Yates, the working array is NOT a dense
 * R-length buffer -- that would make both the malloc and the
 * initialization loop O(R), defeating the point of "partial" for
 * the common case of small L, huge R (e.g. 5?2000000000). Instead,
 * deal_get()/deal_set() below implement a sparse index->value map:
 * any position with no entry implicitly holds its own index (the
 * untouched identity permutation), and only positions actually
 * swapped ever get an entry. At most L entries are ever live, so
 * the table is sized off L, not R -- true O(L) time and memory.
 *
 * The table is a plain sys_malloc()/sys_mfree()'d buffer
 * (linux_platform.c's malloc()/free() wrappers -- not mem_free(),
 * which is the workspace-internal one from mplmem.c), not a
 * workspace temp -- it's freed before this function returns, so it
 * never needs to survive a make_temp()-triggered compaction, and it
 * isn't bounded by WSFULL the way every other array-producing op is.
 * DEAL_MAXL stands in for that missing natural ceiling, sized so the
 * table (roughly 8 bytes/entry at <=50% load factor) stays well
 * under a sane malloc for a scratch buffer. R's ceiling is myrnd()'s,
 * RND_MAXN (2147483646, see externs.h/rndsrc.c): the first draw is
 * myrnd(R), so a larger R is ERR_DOMAIN up front, when L > 0 -- without
 * that check myrnd()'s error return became a table index of i-1, the
 * table's own empty-slot marker. Nothing here is allocated by R. */
#define DEAL_MAXL 2000000L

struct deal_slot { int32_t key; int32_t val; };

/* deal_get/deal_set -- sparse index->value map for do_deal(), above.
 * Open addressing, linear probing; key == -1 marks an empty slot
 * (real keys are always >= 0, since they're offsets into 0..R-1).
 * cap (table length) must be a power of two; mask == cap-1. */
static int32_t
deal_get(struct deal_slot *tbl, int32_t mask, int32_t key)
{
	int32_t h = (int32_t)(((unsigned long)key * 2654435761UL) & (unsigned long)mask);

	while (tbl[h].key != -1L) {
		if (tbl[h].key == key) return(tbl[h].val);
		h = (h + 1) & mask;
	}
	return(key);	/* untouched: still the identity value */
}

static void
deal_set(struct deal_slot *tbl, int32_t mask, int32_t key, int32_t val)
{
	int32_t h = (int32_t)(((unsigned long)key * 2654435761UL) & (unsigned long)mask);

	while (tbl[h].key != -1L && tbl[h].key != key) h = (h + 1) & mask;
	tbl[h].key = key;
	tbl[h].val = val;
}

/* deal_arg -- one operand of do_deal(): a scalar or one-element
 * vector, read as int32_t into *out. Rank > 1 is ERR_RANK; a vector of
 * any other length (empty included) is ERR_LENGTH; a float outside
 * 0..INTEGER_MAX (or NaN -- the test is written so NaN fails it) is
 * ERR_DOMAIN, checked before the conversion, which is otherwise
 * undefined behavior. In-range floats truncate, as before. Negative
 * integers are left to do_deal()'s own range check. */
static short
deal_arg(struct ValEnt *ve, int32_t *out)
{
union MultiP mp;
double d;

	if (ve->ve_rank > 1) return(SET_ERR(ERR_RANK));
	if (ve->ve_rank == 1 && ve->ve_rho[0] != 1L) return(SET_ERR(ERR_LENGTH));
	mp.cp = DATA_AD(ve);
	switch (ve->ve_vtype & BYTES_MASK) {
		case 1:  *out = (int32_t)(signed char)*mp.cp; break;
		case 2:  *out = (int32_t)*mp.sp;               break;
		case 4:  *out = *mp.lp;                        break;
		case FLOATSIZE:
			d = *mp.dp;
			if (!(d >= 0.0 && d <= INTEGER_MAX))
				return(SET_ERR(ERR_DOMAIN));
			*out = (int32_t)d;
			break;
		default: *out = 0; break;	/* not a valid element size */
	}
	return(0);
}

short
do_deal()
{
struct ValEnt *re,*le,*te;
union MultiP mp;
struct deal_slot *tbl;
int32_t l,r,i,j,x,y,origin,cap,mask;
long tblsize;
short vt,e;

	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	/* both operands: scalar or one-element vector -- see deal_arg() */
	if ((e = deal_arg(le, &l)) != 0) return(e);
	if ((e = deal_arg(re, &r)) != 0) return(e);

	if (l < 0L || r < 0L || l > r) return(SET_ERR(ERR_DOMAIN));
	if (l > DEAL_MAXL)             return(SET_ERR(ERR_DOMAIN));
	if (l && r > RND_MAXN)         return(SET_ERR(ERR_DOMAIN));	/* myrnd()'s ceiling */

	/* table capacity: smallest power of two >= 2*l, so the map stays
	   at <=50% load factor; floored at 8 so l==0/l==1 still probe a
	   well-formed table. */
	cap = 8L;
	while (cap < l * 2L) cap <<= 1;
	mask = cap - 1L;

	tblsize = (long)cap * (long)sizeof(struct deal_slot);
	tbl = (struct deal_slot *)sys_malloc(0, tblsize);	/* t is ignored by this platform's wrapper */
	if (!tbl) return(SET_ERR(ERR_WSFULL));
	for (i = 0L; i < cap; i++) tbl[i].key = -1L;

	/* result vtype sized for the largest value it can hold,
	   origin+R-1 -- same sizing convention as monadic ⍳'s OP_IOT */
	origin = (int32_t)wspace->origin;
	vt = BCONST_CODE;
	if ((r - 1L + origin) > 127L)   vt = SCONST_CODE;
	if ((r - 1L + origin) > 32767L) vt = ICONST_CODE;

	VTEMP = (ws_off_t*)0;
	te = make_temp((short)vt, 1, l);
	if (!te) {
		sys_mfree((char *)tbl, tblsize);
		return(SET_ERR(ERR_WSFULL));
	}

	/* partial Fisher-Yates over the sparse identity map: position i
	   is finalized and written out immediately, so it's never looked
	   up again -- only position j (which may be revisited by a later
	   i) needs its swapped-in value recorded. */
	mp.cp = DATA_AD(te);
	for (i = 0L; i < l; i++) {
		j = i + (int32_t)myrnd((unsigned long)(r - i));
		x = deal_get(tbl, mask, i);
		y = deal_get(tbl, mask, j);
		deal_set(tbl, mask, j, x);
		y += origin;
		switch (te->ve_vtype & BYTES_MASK) {
			case 1: *mp.cp++ = (char)y;  break;
			case 2: *mp.sp++ = (short)y; break;
			case 4: *mp.lp++ = y;        break;
		}
	}
	sys_mfree((char *)tbl, tblsize);

	if (VTEMP) stack_pop(3);
	else       stack_pop(2);
	temp_stack(te);
	return(0);
}

/*
	dbl_to_i64() -- convert a double to int64_t without undefined
	behavior: out-of-range values saturate, NaN gives 0. Converting an
	out-of-range double straight to an integer type is undefined in C
	(caught by check_san once V73 sent every dyadic * through the float
	branch: 10*20). Saturating keeps an out-of-range value out of range,
	so any later width check on acc still sees it as too big.
*/
static int64_t
dbl_to_i64(double d)
{
	if (d >= 9223372036854775808.0) return(INT64_MAX);
	if (d < -9223372036854775808.0) return(INT64_MIN);
	if (d != d) return(0);		/* NaN */
	return((int64_t)d);
}

/*
	dbl_to_i32() -- convert a float count, index, axis or other
	whole-number argument to int32_t, truncating toward zero as the
	plain casts it replaces did (V74). Returns 1 with *out set if d is
	finite and fits; 0, leaving *out alone, if not (out of range,
	infinite, NaN), so the caller picks the error: Domain Error for a
	count or axis, Index Error for an index, "not found" for a lookup.
	Same idea as deal_arg() above, which already range-checked before
	converting. Converting an out-of-range double straight to an
	integer type is undefined in C, and long is 32 bits on Windows.
	Shared with monop.c and index.c through externs.h.
*/
short
dbl_to_i32(double d, int32_t *out)
{
	if (!(d > -2147483649.0 && d < 2147483648.0)) return(0);	/* also NaN */
	*out = (int32_t)d;
	return(1);
}

/*
	apply_scalar_dyop()

	Applies dyadic op c to a single (left,right) scalar pair.
	Extracted verbatim from do_dyop()'s per-element switch so it can be
	reused by do_iprod() (generalized inner product) without duplicating
	the arithmetic/logical dispatch. Behavior is unchanged from the
	inline version -- this is a pure refactor, no case bodies altered.

	csize/i/t follow do_dyop()'s existing convention: csize is the
	widest of the three (left, right, result) byte widths, used to pick
	the float vs int branch; i and t are the *input* left/right byte
	widths, used only to decide whether acc/acc2 need promoting into
	dacc/dacc2 before the float switch.

	On entry *accp and *acc2p, *daccp and *dacc2p hold the left
	and right operand values already read out of the source arrays.
	On return *accp (and *daccp) hold the result; *acc2p and *dacc2p are
	left however the original inline code left them -- matching
	original behavior exactly, even though do_dyop() itself doesn't use
	them again afterward either way.

	Returns 0 on success, or a SET_ERR(...) code on domain/syntax error.
*/
short
apply_scalar_dyop(c, csize, i, t, accp, acc2p
	, daccp, dacc2p
	)
UCHAR c;
short csize,i,t;
int64_t *accp,*acc2p;
double *daccp,*dacc2p;
{
int64_t acc,acc2;
double dacc,dacc2;

	acc = *accp;
	acc2 = *acc2p;
	dacc = *daccp;
	dacc2 = *dacc2p;

		if (csize == FLOATSIZE) 
		{
			if (t != FLOATSIZE) dacc2 = (double)acc2;
			if (i != FLOATSIZE) dacc = (double)acc;
			switch (c) 
			{
				case OP_ADD:
					dacc += dacc2;
					break;
				case OP_SUB:
					dacc -= dacc2;
					break;
				case OP_MUL:
					dacc *= dacc2;
					break;
				case OP_DIV:
					if (!dacc2) return(SET_ERR(ERR_DOMAIN));
					dacc /= dacc2;
					break;
				case OP_POW:
					if (dacc < 0.0 && dacc2 != floor(dacc2)) return(SET_ERR(ERR_DOMAIN));
					dacc = pow(dacc,dacc2);
					/* overflow (10*400) or 0 to a negative power:
					   infinity, so Domain Error -- as monadic * */
					if (!isfinite(dacc)) return(SET_ERR(ERR_DOMAIN));
					break;
				case OP_AB:	/* modulo ! */
					dacc = dacc2 - (dacc * floor(dacc2 / dacc));
					/* return(SET_ERR(ERR_DOMAIN)); */
/*
					if (dacc2 < 0L) dacc2 += (1 - dacc2/dacc) * dacc;
					dacc = dacc2 % (long)dacc;
*/
					break;
				case OP_EQ:
					if (dacc == dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_NE:
					if (dacc != dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_GE:
					if (dacc >= dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_LE:
					if (dacc <= dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_LT:
					if (dacc < dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_GT:
					if (dacc > dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_CE:
					if (dacc < dacc2) dacc = dacc2;
					break;
				case OP_FL:
					if (dacc > dacc2) dacc = dacc2;
					break;
				case OP_AND:
					if (dacc && dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_OR:
					if (dacc || dacc2) dacc = 1.0;
					else dacc = 0.0;
					break;
				case OP_NAND:
					if (dacc && dacc2) dacc = 0.0;
					else dacc = 1.0;
					break;
				case OP_NOR:
					if (dacc || dacc2) dacc = 0.0;
					else dacc = 1.0;
					break;
				case OP_BAND:
				case OP_BOR:
					return(SET_ERR(ERR_DOMAIN));
				case OP_LS:
					dacc = dacc * pow(2.0,dacc2);
					break;
				case OP_RS:
					dacc = dacc / pow(2.0,dacc2);
					break;
#ifdef TRIG
				case OP_OH:
					/* L○R, the APL\360 table: L a whole number
					   from ¯7 to 7. The old non-standard 10○ (log10),
					   11○ (ln) and ¯11○ were removed in V72 -- ⍟
					   covers them. Anything outside R's real domain,
					   or a result that overflows, is a Domain Error. */
					if (dacc != floor(dacc) || dacc < -7.0 || dacc > 7.0)
						return(SET_ERR(ERR_DOMAIN));
					switch ((int)dacc)
					{
						case 0:		/* (1-R*2)*.5 */
							if (fabs(dacc2) > 1.0)
								return(SET_ERR(ERR_DOMAIN));
							dacc = sqrt((1.0 - dacc2) * (1.0 + dacc2));
							break;
						case 1:		/* sin */
							dacc = sin(dacc2);
							break;
						case 2:		/* cos */
							dacc = cos(dacc2);
							break;
						case 3:		/* tan */
							dacc = tan(dacc2);
							break;
						case 4:		/* (1+R*2)*.5 -- hypot() can't overflow on R*2 */
							dacc = hypot(1.0, dacc2);
							break;
						case 5:		/* sinh */
							dacc = sinh(dacc2);
							break;
						case 6:		/* cosh */
							dacc = cosh(dacc2);
							break;
						case 7:		/* tanh */
							dacc = tanh(dacc2);
							break;
						case -1:	/* arcsin */
							if (fabs(dacc2) > 1.0)
								return(SET_ERR(ERR_DOMAIN));
							dacc = asin(dacc2);
							break;
						case -2:	/* arccos */
							if (fabs(dacc2) > 1.0)
								return(SET_ERR(ERR_DOMAIN));
							dacc = acos(dacc2);
							break;
						case -3:	/* arctan */
							dacc = atan(dacc2);
							break;
						case -4:	/* (¯1+R*2)*.5, the APL\360 form (never
								   negative); split so R*2 can't overflow */
							if (fabs(dacc2) < 1.0)
								return(SET_ERR(ERR_DOMAIN));
							dacc = sqrt(fabs(dacc2) - 1.0) * sqrt(fabs(dacc2) + 1.0);
							break;
						case -5:	/* arcsinh */
							dacc = asinh(dacc2);
							break;
						case -6:	/* arccosh */
							if (dacc2 < 1.0)
								return(SET_ERR(ERR_DOMAIN));
							dacc = acosh(dacc2);
							break;
						case -7:	/* arctanh -- infinite at R=±1 */
							if (fabs(dacc2) >= 1.0)
								return(SET_ERR(ERR_DOMAIN));
							dacc = atanh(dacc2);
							break;
					}
					if (!isfinite(dacc)) return(SET_ERR(ERR_DOMAIN));
					break;
				case OP_LOG:
					/* L⍟R: log of R to base L. Real logs only; base 1
					   has none. log2 ratio rather than ln ratio: exact
					   for 10⍟1000 and powers of 2 in a sandbox check,
					   where ln/ln gave 2.9999999999999996 for 10⍟1000. */
					if (dacc <= 0.0 || dacc == 1.0 || dacc2 <= 0.0)
						return(SET_ERR(ERR_DOMAIN));
					dacc = log2(dacc2) / log2(dacc);
					break;
#endif
				default:
					return(SET_ERR(ERR_SYNTAX));	/* syntax error */
			}
			/* was (long)dacc / (long)dacc2: undefined for a result
			   or operand beyond long's range (10*20), and long is
			   only 32 bits on Windows -- V73 */
			acc = dbl_to_i64(dacc);
			acc2 = dbl_to_i64(dacc2);
		} else {
			switch (c) 
			{
				case OP_ADD:
					acc += acc2;
					break;
				case OP_SUB:
					acc -= acc2;
					break;
				case OP_MUL:
					acc *= acc2;
					break;
				case OP_DIV:
					if (!acc2) return(SET_ERR(ERR_DOMAIN));
					acc /= acc2;
					break;
				case OP_POW:
					dacc = acc;
					dacc2 = acc2;
					dacc = pow(dacc,dacc2);
					acc = dbl_to_i64(dacc);	/* not reached since V73 (* forced float) */
					break;
				case OP_AB:	/* modulo ! */
/* %%%					if (!acc) return(SET_ERR(ERR_DOMAIN)); */
					if (!acc) { acc = acc2; break; } // Iverson's way
					if (acc2 < 0L) acc2 += (1 - acc2/acc) * acc;
					acc = acc2 % acc;
					break;
				case OP_EQ:
					if (acc == acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_NE:
					if (acc != acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_GE:
					if (acc >= acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_LE:
					if (acc <= acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_LT:
					if (acc < acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_GT:
					if (acc > acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_CE:
					if (acc < acc2) acc = acc2;
					break;
				case OP_FL:
					if (acc > acc2) acc = acc2;
					break;
				case OP_AND:
					if (acc && acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_OR:
					if (acc || acc2) acc = 1L;
					else acc = 0L;
					break;
				case OP_NAND:
					if (acc && acc2) acc = 0L;
					else acc = 1L;
					break;
				case OP_NOR:
					if (acc || acc2) acc = 0L;
					else acc = 1L;
					break;
				case OP_BAND:
					acc = acc & acc2;
					break;
				case OP_BOR:
					acc = acc | acc2;
					break;
				case OP_LS:
					acc = acc << acc2;
					break;
				case OP_RS:
					acc = acc >> acc2;
					break;
				default:
					return(SET_ERR(ERR_SYNTAX));	/* syntax error */
			}
	}

	*accp = acc;
	*acc2p = acc2;
	*daccp = dacc;
	*dacc2p = dacc2;
	return(0);
}

short
do_dyop(c)
UCHAR c;
{
long lscan,rscan,lend,rend;
short scancompl;
union MultiP rp,lp,tp;
struct ValEnt *re,*le,*te;
short b,i,t,csize,err;
int32_t *rho;
int64_t acc,acc2;
double dacc,dacc2;
#ifdef BINS
int lbitc,rbitc,tbitc;
#endif

	VLEFT = get_vparm(0);
	VRIGHT = get_vparm(1);
	if (!VRIGHT || !VLEFT) return(SET_ERR(ERR_SYNTAX));
	if (!*VRIGHT) return(SET_ERR(ERR_VALUE));
	switch (c) 	/* test after get_vparms */
	{
		case OP_ASS:
			if (!(*MSTACK & STACK_VARIABLE)) return(SET_ERR(ERR_SYNTAX));
			if (MSTACK[1] != STACK_TEMP_VAL) 	/* src is variable */
			{
				*VLEFT = WS_OFF(dup_val(VRIGHT));	/* make copy of src */			
				if (!*VLEFT) return(SET_ERR(ERR_WSFULL));
			} else *VLEFT = *VRIGHT;	/* update variable */
			/* update references */
			i = *MSTACK & VNUM_MASK;
			SYMT[i].type = SYM_VAR;		/* make it a variable */
			WSVAL_PTR(*VLEFT)->ve_reference = WS_OFF(VLEFT);
		debug_check_ref("dyop.c/VLEFT", (struct MemEnt *)WSVAL_PTR(*VLEFT), WSVAL_PTR(*VLEFT)->ve_reference); /* %%% DBG */
			WSVAL_PTR(*VLEFT)->ve_vtype &= ~VARIABLE_BIT;	/* clear temp bit */
			swapdrop();		/* leave the variable on stack */
			return(0);
		case OP_RHO:
			return(do_drho(0));
		case OP_TAK:
			return(do_drho(1));
		case OP_DRP:
			return(do_drho(2));
		case OP_RAV:
			if (OUTERFLAG) return(do_ocat());
			return(do_drav());
		case OP_CATF:
			return(do_drav1());
		case OP_COMP:
			return(do_compx(0));
		case OP_EXP:
			return(do_compx(1));
		case OP_COMPF:
			return(do_compx1());
		case OP_EXPF:
			return(do_expand1());
		case OP_IOT:
			return(do_diota());
		case OP_QU:
			return(do_deal());
		case OP_TRA:
			return(do_dtrans());
		case OP_HROT:
			if (OUTERFLAG) return(do_ohrot());
			return(do_dhrot(1));
		case OP_VROT:
			if (OUTERFLAG) return(do_ovrot());
			return(do_dvrot(1));
		case OP_ENC:
			return(do_denc());
		case OP_DEC:
			return(do_ddec());
		case OP_EPS:
			return(do_depsilon());
		case OP_SQUAD:
			return(do_squad());
	}
	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	re = WSVAL_PTR(*VRIGHT);
	le = WSVAL_PTR(*VLEFT);

	rscan = re->ve_length;
	lscan = le->ve_length;
	
	if (!rscan || !lscan) 		/* rho 0 is special case */
	{
		stack_pop(2);		/* pop both off stack */
		te = make_temp((short)BCONST_CODE,1,0L);	/* make an i0 */
		if (!te) return(SET_ERR(ERR_WSFULL));
		return(0);
	}

/* size by default must be the larger of the two lengths by
		the larger of the two types */

/* if a logical operator, or chars are involved */
	if (c < OP_LOGIC || (re->ve_vtype | VARIABLE_BIT) == QUOT_CODE || (le->ve_vtype | VARIABLE_BIT) == QUOT_CODE) 
	{
		rend = 1;			/* result is always 1 byte */
		i = QUOT_CODE;
	} else {
		lend = le->ve_vtype & BYTES_MASK;
		rend = re->ve_vtype & BYTES_MASK;
#ifdef TRIG
		if (c == OP_OH || c == OP_LOG) rend = FLOATSIZE;
		else {
#endif
			rend = lend > rend ? lend : rend;	/* bytes per */
#ifdef TRIG
		}
#endif
		/* )INTDIV OFF / ⎕INTDIV 0: dyadic divide always returns float,
		   same forced-width approach as OP_OH above. csize below then
		   picks up FLOATSIZE and apply_scalar_dyop() takes its float
		   branch, which already promotes integer operands and
		   zero-checks. Covers outer product too (same path, OUTERFLAG).
		   The input-temp reuse check below only matches when an operand
		   is already float, so reusing it as the output stays safe. */
		if (c == OP_DIV && wspace->floatdiv) rend = FLOATSIZE;
		/* Dyadic * always returns float (V73). The integer branch of
		   apply_scalar_dyop() truncated pow()'s result back to an
		   integer, so 2*¯1 gave 0, and converting an out-of-range
		   result (10*20) to long is undefined. Forcing float here, as
		   for OP_OH/OP_LOG, sends every * through the float branch;
		   its integer-branch case is no longer reached from do_dyop()
		   or do_iprod(). */
		if (c == OP_POW) rend = FLOATSIZE;
		i = CONST_CODE;
	}
	lend = lscan > rscan ? lscan : rscan;	/* elements */

	te = (struct ValEnt *)0;
	if (OUTERFLAG) 
	{
		TOTSCAN = rscan * lscan;
		b = re->ve_rank + le->ve_rank;
	} else {	
		TOTSCAN = lend;
		if (lend == lscan && rend == (le->ve_vtype & BYTES_MASK)) te = le;
		else  {
		   if (lend == rscan && rend == (re->ve_vtype & BYTES_MASK)) te = re;
		}
		if (te && !(te->ve_vtype & VARIABLE_BIT)) te = (struct ValEnt *)0; /* dest can't be var */


		/* it would be nice if this wasn't done this way-- if the input temp could potentially upsize
		   given the opcode (c), we don't use the input temp as the output.  I would think that it
		   is possible to do that though, and go ahead and let it resize the input+output in mid-stream,
		   though all the size variables would have to be adjusted as well.  Instead, if this is the case
		   it will cause a separate output temp to be created and thus double the space requirements. */

		if (c >= RESIZE_DESTRUCT) te = (struct ValEnt *)0;	/* could resize */



		b = re->ve_rank;
		if (lend == lscan && !(lend == rscan && b > le->ve_rank)) 
		{
			b = le->ve_rank;
		}
	}

	VTEMP = (ws_off_t*)0;
	if (!te) 		/* if no output yet, create one */
	{
		te = make_temp((short)rend | i,b,TOTSCAN);
		if (!te) return(SET_ERR(ERR_WSFULL));
		re = WSVAL_PTR(*VRIGHT);	/* these may have moved while we alloced */
		le = WSVAL_PTR(*VLEFT);
		if (c < OP_LOGIC) te->ve_vtype = BCONST_CODE;
		if (OUTERFLAG) 
		{
			i = le->ve_rank;
			while (i) te->ve_rho[--b] = le->ve_rho[--i];
			i = re->ve_rank;
			while (i) te->ve_rho[--b] = re->ve_rho[--i];
		} else {
			rho = re->ve_rho;
			if (b == le->ve_rank && !(b == re->ve_rank && rscan > lscan)) 
			{
				rho = le->ve_rho;
			}
			while (b--) te->ve_rho[b] = rho[b];
		}
	} else if (c < OP_LOGIC && rend == 1) te->ve_vtype = BCONST_CODE;

	lend = lscan;
	rend = rscan;
	i = le->ve_vtype & BYTES_MASK;
	t = re->ve_vtype & BYTES_MASK;

	b = te->ve_vtype & BYTES_MASK;

	csize = b;
	if (i > csize) csize = i;
	if (t > csize) csize = t;

	scancompl = 3;
	lscan = lend;
	rscan = rend;
	tp.cp = DATA_AD(te);
	rp.cp = DATA_AD(re);
	lp.cp = DATA_AD(le);
#ifdef BINS
	tbitc = rbitc = lbitc = 0;
#endif
	while (scancompl) 
	{
		dacc = dacc2 = 0.0;
		switch (i) {	/* left entry bytes */
#ifdef BINS
			case 0:
				acc = 1 & (long)(*lp.cp >> lbitc);
				lbitc = 7 & ++lbitc;
				if (!lbitc) lp.cp++;
				break;
#endif
			case 1:
				acc = (long)(signed char)*lp.cp;
				if (!OUTERFLAG) lp.cp++;
				break;
			case 2:
				acc = (long)*lp.sp;
				if (!OUTERFLAG) lp.sp++;
				break;
			case 4:
				acc = (long)*lp.lp;
				if (!OUTERFLAG) lp.lp++;
				break;
			case FLOATSIZE:
				dacc = *lp.dp;
				if (!OUTERFLAG) lp.dp++;
				break;
		}
		switch (t) {	/* right entry bytes */
#ifdef BINS
			case 0:
				acc2 = 1 & (long)(*rp.cp >> rbitc);
				rbitc = 7 & ++rbitc;
				if (!rbitc) rp.cp++;
				break;
#endif
			case 1:
				acc2 = (long)(signed char)*rp.cp++;
				break;
			case 2:
				acc2 = (long)*rp.sp++;
				break;
			case 4:
				acc2 = (long)*rp.lp++;
				break;
			case FLOATSIZE:
				dacc2 = *rp.dp++;
				if (i != FLOATSIZE) dacc = (double)acc;
				break;
		}
		err = apply_scalar_dyop(c, csize, i, t, &acc, &acc2, &dacc, &dacc2);
		if (err) return(err);
		switch (b) 
		{
#ifdef BINS
			case 0:
				if (acc > 1 || acc < 0) 
				{
				acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
				} else {
					if (acc) *ts.cp |= (1 << tbitc);
					else *ts.cp &=  ~(1 << tbitc);
					tbitc = 7 & ++tbitc;
					if (!tbitc) ts.cp++;
				}
				break;
#endif
			case 1:
				if (acc > 127 || acc < -128) 
				{
				acc2 = (int32_t)(tp.cp - (char *)DATA_AD(te));
					if (!upsizef(te,acc,acc2,dacc)) return(SET_ERR(ERR_WSFULL));
					re = WSVAL_PTR(*VRIGHT);	/* these may have moved while we alloced */
					le = WSVAL_PTR(*VLEFT);
					te = WSVAL_PTR(*VTEMP);
					b = te->ve_vtype & BYTES_MASK;
					tp.cp = DATA_AD(te);

					switch (b)
					{
						case 2:
							tp.sp += acc2;
							*tp.sp++ = (short)acc;
							break;
						case 4:
							tp.lp += acc2;
							*tp.lp++ = acc;
							break;
						case FLOATSIZE:
							tp.dp += acc2;
							*tp.dp++ = dacc;
							break;
					}
				} else *tp.cp++ = (char)acc;
				break;
			case 2:
				if (acc > 32767 || acc < -32768) 
				{
					acc2 = (int32_t)(tp.sp - (short *)DATA_AD(te));	/* element index */
					if (!upsizef(te,acc,acc2,dacc)) return(SET_ERR(ERR_WSFULL));
					re = WSVAL_PTR(*VRIGHT);
					le = WSVAL_PTR(*VLEFT);
					te = WSVAL_PTR(*VTEMP);

					b = te->ve_vtype & BYTES_MASK;
					tp.cp = DATA_AD(te);

					switch (b)	
					{
						case 4:
							tp.lp += acc2;
							*tp.lp++ = acc;
							break;
						case FLOATSIZE:
							tp.dp += acc2;
							*tp.dp++ = dacc;
							break;
					}
				} else *tp.sp++ = (short)acc;
				break;
			case 4:
				if (acc > 2147483647L || acc < -2147483648L) 
				{
					acc2 = (int32_t)(tp.lp - (int32_t *)DATA_AD(te));	/* element index */
					/* acc (a long) already holds the true, un-narrowed result on
					   both branches of apply_scalar_dyop -- dacc is only live when
					   an operand was already float. Recompute it here so upsizef()
					   and the store below get the correct value either way. */
					dacc = (double)acc;
					if (!upsizef(te,acc,acc2,dacc)) return(SET_ERR(ERR_WSFULL));
					re = WSVAL_PTR(*VRIGHT);
					le = WSVAL_PTR(*VLEFT);
					te = WSVAL_PTR(*VTEMP);
					b = FLOATSIZE;
					tp.cp = DATA_AD(te);
					tp.dp += acc2;
					*tp.dp++ = dacc;
				} else
				*tp.lp++ = acc;
				break;
			case FLOATSIZE:
				*tp.dp++ = dacc;
				break;
		}
		if (!OUTERFLAG) lscan--;
		rscan--;
		if (!rscan) 
		{
			scancompl &= 2;		/* mask out incomplete bit */
			rscan = rend;		/* wrap around */
			rp.cp = DATA_AD(re);
			if (OUTERFLAG) {
				lscan--;
				switch (i) {
					case 1:
						lp.cp++;
						break;
					case 2:
						lp.sp++;
						break;
					case 4:
						lp.lp++;
						break;
					case FLOATSIZE:
						lp.dp++;
						break;
				}
			}
		}
		if (!lscan) {
			scancompl &= 1;		/* mask out incomplete bit */
			lscan = lend;		/* wrap around */
			lp.cp = DATA_AD(le);
		}
	}

/* pop 'em all, including any extra temp val */
	if (VTEMP) stack_pop(3);
	else stack_pop(2);

	temp_stack(te);
	return(0);
}

