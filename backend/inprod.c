/*
	inprod.c

	Generalized inner product: L f.g R  (e.g. +.x matrix product).

	This is Stage 5 of INNER_PRODUCT_PLAN_V1.md, split into 5a/5b/5c/5d.
	This file now implements 5a, 5b, (scope-narrowed) 5c, and 5d:
		- conformability check, with scalar L/R broadcasting (5d)
		- result rank/shape computation
		- pairwise (g) and folded (f) result type sizing
		- allocation of the result temp via make_temp()
		- the core nested loop: L walked contiguously along its last
		  axis, R walked with first-axis stride, apply_scalar_dyop()
		  for each (l,r) pairing, reduce_identity()/reduce_combine()
		  to fold the n pairwise results together with f (including
		  the n == 0 identity case and overflow growth via upsize())
		- domain check rejecting arithmetic g (op2 >= OP_LOGIC) on
		  char (QUOT_CODE) operands with ERR_DOMAIN, while leaving
		  comparison/logic g on char data alone -- see the plan's V12
		  scope narrowing and the check itself, just after the
		  conformability test, for the reasoning.
		- scalar L and/or R broadcasting against the other side's
		  would-be-contracted axis, with the both-scalar case (no
		  axis at all) folded into the same core loop by forcing
		  n = 1 rather than a separate code path -- see the Stage 5d
		  comments just above do_iprod()'s scalar handling for the
		  reasoning, confirmed against the worked example
		  `10 +.x 3 2 8 = 130`.
	Struct operands and any other Stage 5c edge cases beyond the char
	arithmetic-domain check above are NOT covered here.
*/

#define INPROD_C 1

#include "parse.h"
#include "mplmem.h"
#include "externs.h"

/* file-local helper: cast ws_off_t offset to struct ValEnt*, same
   convention dyop.c/monop.c both use. */
#define WSVAL_PTR(off) ((struct ValEnt *)WS_PTR(off))

/*
	do_iprod()

	op1 = f, the fold/reduce operator (e.g. + in +.x). Restricted at
	fold time (Stage 5b) to whatever monop.c's reduce_identity()/
	reduce_combine() support -- not enforced here.
	op2 = g, the pairwise operator (e.g. x in +.x). Any primitive
	scalar dyadic op that dyop.c's apply_scalar_dyop() supports; not
	restricted to reduce_combine()'s list.

	L and R come off the value stack exactly like do_dyop()'s operands.

	Conformability: for two non-scalar operands, L's last axis
	(le->ve_rho[0]) must equal R's first axis (re->ve_rho[re->ve_rank
	- 1]) -- see INNER_PRODUCT_PLAN_V1.md's "Axis identification",
	derived from the outer-product shape-build code in do_dyop()'s
	OUTERFLAG branch. A rank-0 (scalar) operand is an exception (Stage
	5d): it has no axis of its own, so it broadcasts against whichever
	axis it would otherwise have needed to match, rather than being
	length-checked. A length-1 *vector* is not a scalar and still goes
	through the exact-match check -- see the plan's Stage 5d note on
	why `,10` and `10` are deliberately not treated the same.
*/
short
do_iprod(op1, op2)
UCHAR op1, op2;
{
struct ValEnt *le, *re, *te;
short lrank, rrank, rank, b, i;
short lscalar, rscalar, bothscalar;
short lbytes, rbytes, csize, gbytes, vt;
short err;
int32_t n, llen, rlen, totlen;
int32_t la, ra, k, outidx, loff, roff;
long gacc, gacc2;
int32_t facc;
union MultiP lbase, rbase, tbase;
double gdacc, gdacc2;
double dfacc;

	VLEFT = get_vparm(0);
	VRIGHT = get_vparm(1);
	if (!VRIGHT || !VLEFT) return(SET_ERR(ERR_SYNTAX));
	if (!*VRIGHT || !*VLEFT) return(SET_ERR(ERR_VALUE));

	le = WSVAL_PTR(*VLEFT);
	re = WSVAL_PTR(*VRIGHT);

	lrank = le->ve_rank;
	rrank = re->ve_rank;

	/* Stage 5d -- scalar L/R broadcasting. A scalar (rank 0) operand
	   contributes no axes to the result and has no axis to contract
	   over -- it broadcasts against whichever axis it would otherwise
	   have needed to conform to, instead of requiring an exact length
	   match. Confirmed against a real worked example, `10 +.x 3 2 8`
	   = 130 (see the plan's Stage 5d section).

	   If only one side is scalar, n (the contracted length) comes
	   from the *other* side's would-be-contracted axis, and the loop
	   below reads the scalar side's single element repeatedly instead
	   of advancing through it. If *both* sides are scalar, there's no
	   axis at all -- n is forced to 1 here so the same k-loop below
	   runs exactly once (seed, no combine: reduce_combine() is only
	   reached for k < n-1, which never happens when n==1), giving
	   "apply g once, no f-fold" for free without a separate code
	   path -- distinct from the n==0 case just below (a real, merely
	   empty, axis that still folds to f's identity). */
	lscalar = (short)(lrank == 0);
	rscalar = (short)(rrank == 0);
	bothscalar = (short)(lscalar && rscalar);

	if (bothscalar) {
		n = 1L;
	} else if (lscalar) {
		n = re->ve_rho[rrank - 1];
	} else if (rscalar) {
		n = le->ve_rho[0];
	} else {
		/* Conformability (ordinary, non-broadcast case). */
		n = le->ve_rho[0];
		if (n != re->ve_rho[rrank - 1]) return(SET_ERR(ERR_LENGTH));
	}

	/* Stage 5c -- arithmetic g on char operands is a domain error.
	   Comparison/logic g (op2 < OP_LOGIC: =,≠,<,≤,>,≥,^,v,...) on char
	   data is fine -- it's a legitimate byte-value comparison, already
	   exercised and confirmed working for ^.= table lookup (V12 in the
	   plan), and real-value ordering comparisons (</≤/>/≥) fall under
	   the same op2<OP_LOGIC test so they're allowed here too, same as
	   =/≠. Arithmetic g (op2>=OP_LOGIC: +,-,x,...) on char data has no
	   sensible APL meaning -- without this check 5b's read-switch would
	   silently treat QUOT_CODE bytes as tiny signed integers and
	   produce a meaningless numeric result. QUOT_CODE test mirrors the
	   one already used below for gbytes sizing. */
	if (op2 >= OP_LOGIC
	 && (((le->ve_vtype | VARIABLE_BIT) == QUOT_CODE)
	  || ((re->ve_vtype | VARIABLE_BIT) == QUOT_CODE)))
	{
		return(SET_ERR(ERR_DOMAIN));
	}

	/* Result rank: (L's axes but its last) ++ (R's axes but its
	   first) -- a scalar side contributes 0 rather than -1 (Stage
	   5d: max(lrank-1,0) / max(rrank-1,0), not the unconditional
	   lrank-1/rrank-1 the non-broadcast case used). */
	rank = (short)((lscalar ? 0 : lrank - 1) + (rscalar ? 0 : rrank - 1));

	/* Elements contributed by each side, contracted axis excluded --
	   computed directly (not le->ve_length/n) so a zero-length
	   contracted axis (n == 0, the fold's identity case, left for
	   Stage 5b) doesn't divide by zero here. These are the same
	   per-axis products the shape-fill loop below needs, so they're
	   computed the same way. */
	llen = 1L;
	for (i = 1; i < lrank; i++) llen *= le->ve_rho[i];
	rlen = 1L;
	for (i = 0; i < rrank - 1; i++) rlen *= re->ve_rho[i];
	totlen = llen * rlen;

	/* Actual per-element storage widths of L and R -- needed below
	   regardless of which g-sizing branch is taken, since Stage 5b's
	   data reads have to use L/R's real widths even when g's *output*
	   is forced to 1 byte (a comparison/logic g on wide/float operands
	   still has to compare at the operands' real width). csize is the
	   width apply_scalar_dyop() should compute at -- do_dyop()'s own
	   "widest of left/right" convention (see do_dyop()'s csize/i/t
	   setup just before its scan loop) -- which is NOT always the same
	   as gbytes below (gbytes is g's *storage* width, forced to 1 for
	   comparisons even when csize is wider, e.g. FLOATSIZE). */
	lbytes = le->ve_vtype & BYTES_MASK;
	rbytes = re->ve_vtype & BYTES_MASK;
	csize = (lbytes > rbytes) ? lbytes : rbytes;
	/* )INTDIV OFF / ⎕INTDIV 0: g = divide always computes and stores
	   float -- mirrors the OP_DIV width override in do_dyop(). gbytes
	   below follows csize (OP_DIV is not < OP_LOGIC), so the result
	   type becomes FCONST_CODE unless f is a comparison/logic op. */
	if (op2 == OP_DIV && wspace->floatdiv) csize = FLOATSIZE;

	/* g's pairwise result byte width -- the same promotion rule
	   do_dyop() uses for its own single elementwise op (see
	   do_dyop(), the "if a logical operator, or chars are involved"
	   block just after the rho-0 special case), applied here to op2
	   instead of do_dyop()'s c. Char/struct operands are sized here
	   like do_dyop() would size them, but whether f.g rejects them
	   outright is Stage 5c's call, not this stage's -- see the plan's
	   Stage 5c note. */
	if (op2 < OP_LOGIC
	 || (re->ve_vtype | VARIABLE_BIT) == QUOT_CODE
	 || (le->ve_vtype | VARIABLE_BIT) == QUOT_CODE)
	{
		gbytes = 1;
	} else {
		gbytes = csize;
	}

	/* f's accumulated result type. If f is one of reduce_combine()'s
	   comparison/logic ops (EQ/NE/LE/GE/GT/LT/AND/OR -- the same
	   "op < OP_LOGIC" test do_dyop() uses to force its own boolean
	   result), the fold always lands on 0 or 1 no matter how wide g's
	   pairwise result was, so the temp is 1 byte regardless of gbytes.
	   Otherwise (ADD/SUB/MUL/CE/FL) the fold starts at g's pairwise
	   width and may need to grow -- exactly monop.c's scanred()/
	   scanredf() pattern of seeding the temp at the input width and
	   calling upsize() mid-fold on overflow. Doing that growth is
	   Stage 5b's job (it needs real accumulator values); here it's
	   only the starting width.

	   Stage 5d: when both L and R are scalar there's no f-fold at all
	   (see the n==1 note above) -- the result is just g's own value,
	   so it always takes g's own type (the gbytes switch below),
	   regardless of what f is or whether f is even a comparison op. */
	if (bothscalar || op1 >= OP_LOGIC) {
		switch (gbytes) {
			case 1: vt = BCONST_CODE; break;
			case 2: vt = SCONST_CODE; break;
			case FLOATSIZE: vt = FCONST_CODE; break;
			default: vt = ICONST_CODE; break;
		}
	} else {
		vt = BCONST_CODE;
	}

	VTEMP = (ws_off_t*)0;
	te = make_temp(vt, rank, totlen);
	if (!te) return(SET_ERR(ERR_WSFULL));
	re = WSVAL_PTR(*VRIGHT);	/* may have moved while we alloced */
	le = WSVAL_PTR(*VLEFT);

	/* Fill the result shape: R's non-first axes at the low ve_rho
	   indices, then L's non-last axes at the high indices -- mirrors
	   the "R's block occupies the low indices, L's block the high
	   indices" layout do_dyop()'s OUTERFLAG shape-build code uses for
	   L o.g R, just skipping R's first axis and L's last axis instead
	   of copying each operand's whole shape. */
	b = 0;
	for (i = 0; i < rrank - 1; i++) te->ve_rho[b++] = re->ve_rho[i];
	for (i = 1; i < lrank; i++)    te->ve_rho[b++] = le->ve_rho[i];

	/*
		Stage 5b -- the core nested loop.

		L's data is le->ve_length == llen*n elements, contiguous, with
		the contracted axis (length n) fastest-varying (le->ve_rho[0]
		is L's last, contiguous axis, per this codebase's ve_rho
		convention -- see do_compx1's ve_rho[j-1]/first-axis comment
		in monop.c). So L's k-th contracted element of row la sits
		at linear index la*n + k.

		R's data is re->ve_length == n*rlen elements, with the
		contracted axis (re->ve_rho[rrank-1], R's *first* axis) the
		SLOWEST-varying -- consecutive elements along it are rlen
		apart. So R's k-th contracted element of "column" ra sits at
		linear index k*rlen + ra.

		Output element (la,ra) lands at linear index la*rlen + ra,
		matching the R's-shape-fills-the-low-(fast)-ve_rho-indices,
		L's-the-high-(slow)-ones layout already used above for
		te->ve_rho[] -- i.e. ra is the fast-varying output index, la
		the slow one, so la outer / ra inner walks te's data in
		strict increasing order.

		The k-fold itself walks k from n-1 down to 0 (not 0 up to
		n-1): reduce_combine()'s OP_SUB direction handling (sdir<0
		computes acc = new - acc) only gives the correct right-to-left
		APL reduce semantics -- matching monop.c's own scanred()
		OP_REDUCTION walk, which processes its axis in the same
		decreasing-index order for the same reason -- if paired with
		a decreasing physical walk. The plan's sketch didn't spell
		this out; confirmed against scanred() before writing this.
	*/
	lbase.cp = DATA_AD(le);
	rbase.cp = DATA_AD(re);
	tbase.cp = DATA_AD(te);
	outidx = 0L;

	for (la = 0L; la < llen; la++) {
		for (ra = 0L; ra < rlen; ra++) {

			if (n == 0L) {
				/* Zero-length contracted axis: no pairs to walk,
				   seed straight from f's identity element -- same
				   branch scanred/scanredf use for their own
				   zero-length axis case. */
				err = reduce_identity(op1, (UCHAR)gbytes, &facc, &dfacc);
				if (err) return(err);
			} else {
				for (k = n - 1L; k >= 0L; k--) {
					/* Stage 5d: a scalar side's address doesn't
					   advance with k -- it's the same single element
					   every pairing -- instead of the ordinary
					   la*n+k / k*rlen+ra strided addressing. */
					loff = lscalar ? 0L : la * n + k;
					roff = rscalar ? 0L : k * rlen + ra;

					gdacc = gdacc2 = 0.0;
					/* Read L[loff] at its real width. QUOT_CODE
					   (char data) reads as BYTES_MASK==1, same slot
					   as BCONST_CODE -- domain-checking that char
					   data actually belongs in an arithmetic f.g is
					   Stage 5c's job, not this loop's; this only
					   guarantees no out-of-switch undefined read. */
					switch (lbytes) {
						case 1:
							gacc = (long)(signed char)lbase.cp[loff];
							break;
						case 2:
							gacc = (long)lbase.sp[loff];
							break;
						case 4:
							gacc = (long)lbase.lp[loff];
							break;
						case FLOATSIZE:
							gdacc = lbase.dp[loff];
							break;
					}
					/* Read R[roff] at its real width. */
					switch (rbytes) {
						case 1:
							gacc2 = (long)(signed char)rbase.cp[roff];
							break;
						case 2:
							gacc2 = (long)rbase.sp[roff];
							break;
						case 4:
							gacc2 = (long)rbase.lp[roff];
							break;
						case FLOATSIZE:
							gdacc2 = rbase.dp[roff];
							break;
					}

					err = apply_scalar_dyop(op2, csize, lbytes, rbytes,
						&gacc, &gacc2, &gdacc, &gdacc2);
					if (err) return(err);

					if (k == n - 1L) {
						/* first (highest-index) pair -- seed, no
						   combine yet, same "don't operate first
						   time" structure scanred uses. */
						facc = (int32_t)gacc;
						dfacc = gdacc;
					} else {
						err = reduce_combine(op1, -1, (UCHAR)gbytes,
							&facc, (int32_t)gacc, &dfacc, gdacc);
						if (err) return(err);
					}
				}
			}

			/* Write facc/dfacc out at te's *current* width, growing
			   via upsize() on overflow -- same bounds-check style as
			   scanred's/OP_AB's write-out switch. Only reachable when
			   op1 >= OP_LOGIC: if op1 < OP_LOGIC, vt was pinned to
			   BCONST_CODE back in 5a and reduce_combine's comparison
			   ops always leave facc as clean 0/1, so b == 1 and the
			   bounds check below is never true -- intentional, not
			   an oversight. If gbytes == FLOATSIZE, b stays
			   FLOATSIZE forever (case FLOATSIZE below never upsizes)
			   for the same reason scanred's own FLOATSIZE case
			   doesn't. */
			b = te->ve_vtype & BYTES_MASK;
			switch (b) {
				case 1:
					if (facc > 127L || facc < -128L) {
						if (!upsize(te, facc, outidx)) return(SET_ERR(ERR_WSFULL));
						re = WSVAL_PTR(*VRIGHT);
						le = WSVAL_PTR(*VLEFT);
						te = WSVAL_PTR(*VTEMP);
						lbase.cp = DATA_AD(le);
						rbase.cp = DATA_AD(re);
						tbase.cp = DATA_AD(te);
						b = te->ve_vtype & BYTES_MASK;
						switch (b) {
							case 2:
								tbase.sp[outidx] = (short)facc;
								break;
							case 4:
								tbase.lp[outidx] = facc;
								break;
							case FLOATSIZE:
								tbase.dp[outidx] = dfacc;
								break;
						}
					} else {
						tbase.cp[outidx] = (char)facc;
					}
					break;
				case 2:
					if (facc > 32767L || facc < -32768L) {
						if (!upsize(te, facc, outidx)) return(SET_ERR(ERR_WSFULL));
						re = WSVAL_PTR(*VRIGHT);
						le = WSVAL_PTR(*VLEFT);
						te = WSVAL_PTR(*VTEMP);
						lbase.cp = DATA_AD(le);
						rbase.cp = DATA_AD(re);
						tbase.cp = DATA_AD(te);
						b = te->ve_vtype & BYTES_MASK;
						switch (b) {
							case 4:
								tbase.lp[outidx] = facc;
								break;
							case FLOATSIZE:
								tbase.dp[outidx] = dfacc;
								break;
						}
					} else {
						tbase.sp[outidx] = (short)facc;
					}
					break;
				case 4:
					/* doesn't auto-promote to FLOAT -- same as
					   scanred's own case 4 (see its comment). */
					tbase.lp[outidx] = facc;
					break;
				case FLOATSIZE:
					tbase.dp[outidx] = dfacc;
					break;
			}

			outidx++;
		}
	}

	if (VTEMP) stack_pop(3);
	else       stack_pop(2);
	temp_stack(te);
	return(0);
}
