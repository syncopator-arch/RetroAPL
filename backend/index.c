/*
	index.c
*/

#define INDEX_C 1

#include "parse.h"
#include "mplmem.h"
#include "externs.h"

extern short do_dyop();
extern short do_monop();
extern ws_off_t *get_vparm();			/* was struct ValEnt ** */
extern long compsize();
extern struct ValEnt *dup_val(ws_off_t *);

/* WS_PTR returns struct MemEnt*; use WSVAL_PTR wherever a ValEnt* is needed */
#define WSVAL_PTR(off)  ((struct ValEnt *)((char *)wspace + (off)))

struct VIndex {
	intptr_t point;
	long count;
	long max;
	struct ValEnt *vp;
};

/* handles indexing (0) and indexed assignments (1).
   returns error code or 0 to caller
	1 - out of memory
	4 - undefined variable 
    6 - indexing error
*/
short
do_index()
{
REG short i,j;
REG long m;
short k,origin,rank,adj;
int32_t *sw,*tm;				/* was long* */
long l,a,tot,rscan = 0,rend = 0;	/* both: used on the assignment path only;
					   rend set for non-struct assignment only */
struct ValEnt *ve,*re = 0L,*tve;
union MultiP sp,ip,dp,rp;
struct VIndex *vi;
short f,stf,foff;
char *rho_array;
int vfloat;
double dacc = 0.0,*dpp;

	rho_array = BSCRATCH;
	f = 0;
	rp.cp = dp.cp = 0L;	/* rp: assignment path only; dp: indexing path only */
	vfloat = -1;
	VLEFT = get_vparm(0);		/* get element being indexed; VLEFT is ws_off_t* */
	if (!VLEFT) return(SET_ERR(ERR_SYNTAX));
	if (!*VLEFT) return(SET_ERR(ERR_VALUE));
	if (*MSTACK == STACK_TEMP_VAL) foff = 2;
	else foff = 1;

	ve = WSVAL_PTR(*VLEFT);		/* dereference ws_off_t to get live ValEnt* */

	origin = wspace->origin;

/* wade through stack to find everything */

	sw = MSTACK + foff;
	l = 1;	/* accumulated size of output */
	rank = 0;	/* accumulation of output rank */
	i = ve->ve_rank - 1;
	/* a scalar can't be indexed -- the scan below would end with the same
	   ERR_INDEX, but only after writing rho_array[-1] */
	if (i < 0) return(SET_ERR(ERR_INDEX));
	rho_array[i] = 0;
	while (*sw != STACK_EMARK) {
		ve = 0L;
		if (*sw == INDEX_MARK) {
			tm = sw + 1;		/* remember where */
			break;				/* regular indexing */
		}			
		if (*sw == INDEXA_MARK) {
			f = 1;				/* assignment */
			LAST_OPERATOR = OP_ASS;
			tm = sw + 1;		/* remember where */
			VRIGHT = 0L;
			if (tm[0] == STACK_TEMP_VAL) VRIGHT = (ws_off_t *)&tm[1];
			if (tm[0] & STACK_VARIABLE) {
				VRIGHT = &SYMT[tm[0] & VNUM_MASK].value;
			}
			if (tm[0] & STACK_FLOATV) {
				VRIGHT = &FLOATVS[tm[0] & FLOATVS_MASK];
			}
			if (!VRIGHT) return(SET_ERR(ERR_SYNTAX));
			break;
		}
		if (*sw == STACK_MARK) {
			i--;
			if (i < 0) return(SET_ERR(ERR_INDEX));
			rho_array[i] = 0;
		}
		if (*sw & STACK_VARIABLE) {
			/* SYMT[].value is ws_off_t */
			ws_off_t off = SYMT[(short)*sw & VNUM_MASK].value;
			if (!off) return(SET_ERR(ERR_VALUE));
			ve = WSVAL_PTR(off);
		}
		if (*sw & STACK_FLOATV) {
			ws_off_t off = FLOATVS[*sw & FLOATVS_MASK];
			if (!off) return(SET_ERR(ERR_VALUE));
			ve = WSVAL_PTR(off);
		}
		if (*sw == STACK_TEMP_VAL) {
			sw++;	/* skip 2 */
			ve = WSVAL_PTR(*sw);	/* stack slot holds ws_off_t */
		}
		if (ve) {
			l *= ve->ve_length;
			rank += ve->ve_rank;
			rho_array[i] = 1;	/* keep track of existing indexes */
		}
		sw++;
	}
	if (i != 0) return(SET_ERR(ERR_INDEX));

	ve = WSVAL_PTR(*VLEFT);
	i = ve->ve_rank;

/* add the null indexes */
	for (j=0;j<i;j++) {
		if (!rho_array[j]) {
			l *= ve->ve_rho[j];
			rank++;
		}
	}
	tot = l;	/* total number of cells involved */

	if ((ve->ve_vtype | 0x80) == STRUCT_CODE) stf = 1;
	else {
		stf = 0;
		if (!f) {
			/* allocate memory for output array */
			/* done this way because make_temp puts it on stack */
			/* (and uses VTEMP) */
			/* and we don't want it on stack here */
			l = compsize(ve->ve_vtype,rank,l);
			/*
			 * MakeEnt third arg is now ws_off_t* -- pass &TMP_VE.
			 * TMP_VE is ws_off_t; MakeEnt sets *p = WS_OFF(new entry).
			 * Cast so the pointer type matches the ws_off_t* parameter.
			 */
			MakeEnt(l, ME_VAL, &TMP_VE);
			if (!TMP_VE) return(SET_ERR(ERR_WSFULL));
			ve = WSVAL_PTR(*VLEFT);	/* refresh after potential realloc */
			{
				struct ValEnt *tv = WSVAL_PTR(TMP_VE);
				tv->ve_vtype  = ve->ve_vtype | VARIABLE_BIT;
				tv->ve_rank   = rank;
				tv->ve_length = tot;
				tv->ve_point  = (int32_t)((char *)&tv->ve_rho[rank] - (char *)tv);
			}
		} else {
			/* check to see if sizes work */
			re = WSVAL_PTR(*VRIGHT);
			if ((re->ve_vtype & BYTES_MASK) > (ve->ve_vtype & BYTES_MASK)) {
				/* Destination is narrower than the assigned value's type.
				   Widen ve's storage in place rather than rejecting the
				   assignment -- allocate a new ValEnt at the wider width,
				   up-convert every existing element into it, then repoint
				   the sole owner slot (*VLEFT) at the new entry. The old
				   entry is left for GarbCollect: once *VLEFT no longer
				   points back at it, its me_reference no longer matches,
				   so the next GC pass reclaims it automatically. */
				short new_w, old_w, jj;
				long tot2, l2;
				struct ValEnt *ne;
				union MultiP op, np;

				new_w = re->ve_vtype & BYTES_MASK;
				old_w = ve->ve_vtype & BYTES_MASK;
				tot2 = ve->ve_length;
				l2 = compsize(new_w, ve->ve_rank, tot2);

				/* Owner arg is NULL here on purpose: MakeEnt() may
				   GarbCollect() internally, which can relocate ve
				   *before* ne is even carved out. Passing VLEFT
				   directly would have MakeEnt overwrite *VLEFT to
				   point at ne immediately, losing our only way back
				   to ve's (possibly just-moved) current address. */
				ne = (struct ValEnt *)MakeEnt(l2, ME_VAL, (ws_off_t *)0);
				if (!ne) return(SET_ERR(ERR_WSFULL));

				/* refresh after potential GC/realloc inside MakeEnt */
				ve = WSVAL_PTR(*VLEFT);
				re = WSVAL_PTR(*VRIGHT);

				ne->ve_vtype  = (ve->ve_vtype & ~BYTES_MASK) | new_w;
				ne->ve_rank   = ve->ve_rank;
				for (jj = 0; jj < ve->ve_rank; jj++)
					ne->ve_rho[jj] = ve->ve_rho[jj];
				ne->ve_length = tot2;
				ne->ve_point  = (int32_t)(CDATA_AD(ne) - (char *)ne);

				op.cp = DATA_AD(ve);
				np.cp = DATA_AD(ne);
				while (tot2--) {
					switch (old_w) {
						case 1:
							l = (long)(signed char)*op.cp++;
							break;
						case 2:
							l = (long)*op.sp++;
							break;
						case 4:
							l = (long)*op.lp++;
							break;
						case FLOATSIZE:
							dacc = *op.dp++;
							break;
					}
					switch (new_w) {
						case 1:
							*np.cp++ = (char)l;
							break;
						case 2:
							*np.sp++ = (short)l;
							break;
						case 4:
							*np.lp++ = l;
							break;
						case FLOATSIZE:
							if (old_w != FLOATSIZE) dacc = (double)l;
							*np.dp++ = dacc;
							break;
					}
				}

				*VLEFT = WS_OFF(ne);
				ne->ve_reference = WS_OFF(VLEFT);
				ve = ne;	/* continue with the widened destination */
			}
			rend = re->ve_length;
		}
	}

/* allocate temp indexing entry */
	l = (long)i * sizeof(struct VIndex);
	/*
	 * TMP_ME is ws_off_t; pass &TMP_ME so MakeEnt can set it.
	 * After this call, vi = WS_PTR(TMP_ME) + sizeof(MemEnt).
	 */
	MakeEnt(l, ME_ITMP, &TMP_ME);
	if (!TMP_ME) return(SET_ERR(ERR_WSFULL));
	vi = (struct VIndex *)((char *)WS_PTR(TMP_ME) + sizeof(struct MemEnt));

/* NOTE:  ALL MEMORY ALLOCATIONS AFTER THIS MUST RESTORE vi FROM TMP_ME */
/* due to the fact that the index entry could move */

	ve = WSVAL_PTR(*VLEFT);	/* get up to date with what we're indexing */
	stack_pop(1);			/* and pop its reference off stack */
	if (f) re = WSVAL_PTR(*VRIGHT);	/* get up to date addr of assign entry */

/* get what we need off stack */
	i = ve->ve_rank - 1;
	k = rank - 1;
	vi[i].count = ve->ve_rho[i];
	vi[i].vp = 0L;
	while (*MSTACK != STACK_EMARK) {
		tve = 0L;
		if (*MSTACK == INDEX_MARK || *MSTACK == INDEXA_MARK) {
			*MSTACK++ = 0;	/* pop it */
			break;			/* indexing a temp */
		}
		if (*MSTACK == STACK_MARK) {
			if (!stf && !f && !rho_array[i]) {
				WSVAL_PTR(TMP_VE)->ve_rho[k--] = ve->ve_rho[i];
			}
			i--;
			vi[i].count = ve->ve_rho[i];
			vi[i].vp = 0L;
		}
		if (*MSTACK & STACK_VARIABLE) {
			ws_off_t off = SYMT[(short)*MSTACK & VNUM_MASK].value;
			tve = off ? WSVAL_PTR(off) : 0L;
		}
		if (*MSTACK & STACK_FLOATV) {
			ws_off_t off = FLOATVS[(short)*MSTACK & FLOATVS_MASK];
			tve = off ? WSVAL_PTR(off) : 0L;
		}
		if (*MSTACK == STACK_TEMP_VAL) {
			*MSTACK++ = 0;		/* skip 2 if temp */
			tve = WSVAL_PTR(*MSTACK);	/* stack slot holds ws_off_t */
		}
		if (tve) {
			vi[i].vp = tve;	/* live pointer in local VIndex array */
			/* update reference so GarbCollect can adjust this slot */
			if (tve->ve_vtype & VARIABLE_BIT) {
				tve->ve_reference = WS_OFF(&vi[i].vp);
				debug_check_ref("index.c/vi.vp", (struct MemEnt*)tve, tve->ve_reference); /* %%% DBG */
			}
			l = 1;
			j = tve->ve_rank - 1;
			for (;j > -1;j--) {
				l *= tve->ve_rho[j];
				if (!f && !stf) {
					WSVAL_PTR(TMP_VE)->ve_rho[k--] = tve->ve_rho[j];
				}
			}
			vi[i].count = l;
			vi[i].max   = l;
			vi[i].point = (intptr_t)DATA_AD(tve);
		}
		*MSTACK++ = 0;	/* pop it */
	}
	if (!f && !stf && !rho_array[i]) WSVAL_PTR(TMP_VE)->ve_rho[k--] = ve->ve_rho[i];
	i = ve->ve_rank;

	if (f) {
		/* init src pointer */
		rp.cp = DATA_AD(re);
		rscan = rend;
	} else {
		/* init dest ptr */
		if (!stf) dp.cp = DATA_AD(WSVAL_PTR(TMP_VE));
	}
/* Validate every explicit index value against its axis bound now,
   independent of tot -- if any null-indexed axis has size 0, tot
   drops to 0 and the gather/scatter loop below never runs, which
   would otherwise let an out-of-range explicit index on another
   axis slip through silently instead of raising ERR_INDEX. */
	for (j = 0; j < i; j++) {
		if (!rho_array[j] || !vi[j].vp) continue;
		sp.cp = DATA_AD(vi[j].vp);
		for (l = 0; l < vi[j].max; l++) {
			switch (vi[j].vp->ve_vtype & BYTES_MASK) {
				case 1:
					m = (long)(signed char)(*sp.cp++);
					break;
				case 2:
					m = (long)(*sp.sp++);
					break;
				case 4:
					m = (long)(*sp.lp++);
					break;
				case FLOATSIZE:
					m = (long)(*sp.dp++);
					break;
				default:	/* not a valid element size -- never pick an element */
					return(SET_ERR(ERR_SYSTEM));
			}
			m -= origin;
			if (m < 0L || m >= ve->ve_rho[j]) return(SET_ERR(ERR_INDEX));
		}
	}

/* init upper indexes */
	if (vi[0].vp) k = vi[0].vp->ve_vtype & BYTES_MASK;
	vi[0].count = 0L;	/* cause upper index init */
	while (tot) {	/* while still operating */
		if (!vi[0].count) {
			if (rho_array[0]) {
				if (i > 1) {
					vi[0].count = vi[0].max;
					vi[0].point = (intptr_t)DATA_AD(vi[0].vp);
				}
			} else vi[0].count = ve->ve_rho[0];
		/* reinit upper indexes */
			l = 0;
			a = 1;
			adj = 1;
			for (j=1;j<i;j++) {
				a *= ve->ve_rho[j-1];		/* determine weighting */
				if (rho_array[j]) {
					sp.cp = (char *)vi[j].point;
					switch (vi[j].vp->ve_vtype & BYTES_MASK) {
						case 1:
							m = (long)(signed char)(*sp.cp++);
							break;
						case 2:
							m = (long)(*sp.sp++);
							break;
						case 4:
							m = (long)(*sp.lp++);
							break;
						case FLOATSIZE:
							m = ((long)*sp.dp++);
							break;
						default:	/* not a valid element size -- never pick an element */
							return(SET_ERR(ERR_SYSTEM));
					}
					m -= origin;
					if (m < 0L || m >= ve->ve_rho[j]) return(SET_ERR(ERR_INDEX));
					l += a * m;
					if (adj) {	/* only if we're supposed to increment */
						vi[j].point = (intptr_t)sp.cp;
						vi[j].count--;
						if (vi[j].count) {
							adj = 0;
						} else {
							vi[j].point = (intptr_t)DATA_AD(vi[j].vp);
						}
					}
				} else {	/* a null index */
					l += a * (ve->ve_rho[j] - vi[j].count);
					if (adj) {	/* only if we're supposed to increment */
						if (vi[j].count) vi[j].count--;
						if (!vi[j].count) vi[j].count = ve->ve_rho[j];
						else adj = 0;
					}
				}
			}
		}
		if (rho_array[0]) {
			ip.cp = (char *)vi[0].point;
			switch (k) {	/* column param's byte count */
				case 1:
					m = (long)(signed char)*ip.cp++;
					break;
				case 2:
					m = (long)*ip.sp++;
					break;
				case 4:
					m = *ip.lp++;
					break;
				case FLOATSIZE:
					/* was *ip.lp++ -- read 4 bytes of the double and
					   advanced only 4, so float indices on this axis
					   (the only axis of a vector) came out as garbage
					   after passing the validation pass above, which
					   reads them correctly through dp. */
					m = ((long)*ip.dp++);
					break;
				default:	/* not a valid element size -- never pick an element */
					return(SET_ERR(ERR_SYSTEM));
			}
			m -= origin;
			if (m < 0L || m >= ve->ve_rho[0]) return(SET_ERR(ERR_INDEX));
			vi[0].point = (intptr_t)ip.cp;
			vi[0].count--;
		} else {
			m = ve->ve_rho[0] - vi[0].count;
			if (vi[0].count) vi[0].count--;
		}
		m += l;		/* add upper indexes offset */
		if (m < 0L) return(SET_ERR(ERR_INDEX));
		sp.cp = DATA_AD(ve);
		if (f) {
			if (!stf) {
				a = 0;	/* a float source loads dacc instead; an integer
					   destination never gets one (it's widened first) */
				switch (re->ve_vtype & BYTES_MASK) {
					case 1:
						a = (long)(signed char)*rp.cp++;
						break;
					case 2:
						a = (long)*rp.sp++;
						break;
					case 4:
						a = (long)*rp.lp++;
						break;
					case FLOATSIZE:
						dacc = *rp.dp++;	
						break;
				}
				/* source was a narrow int but destination is float --
				   promote so the FLOATSIZE write case below doesn't
				   read a stale/uninitialized dacc. Mirrors the same
				   true-width promotion already done internally by
				   apply_scalar_dyop() in dyop.c. */
				if ((ve->ve_vtype & BYTES_MASK) == FLOATSIZE &&
				    (re->ve_vtype & BYTES_MASK) != FLOATSIZE)
					dacc = (double)a;
				switch (ve->ve_vtype & BYTES_MASK) {
					case 1:
						*(sp.cp + m) = (char)a;
						break;
					case 2:
						*(sp.sp + m) = (short)a;
						break;
					case 4:
						*(sp.lp + m) = a;
						break;
					case FLOATSIZE:
						*(sp.dp + m) = dacc; 
						break;
				}
			} else {
				re = WSVAL_PTR(*VRIGHT);
				/* dup the source if it's not a temp */
				if (!(re->ve_vtype & VARIABLE_BIT)) {
					re = dup_val(VRIGHT);
					if (!re) return(SET_ERR(ERR_WSFULL));

					ve = WSVAL_PTR(*VLEFT);	/* restore after possible realloc */
					sp.cp = DATA_AD(ve);
					if (TMP_ME) {
						/* restore the index entry after possible realloc */
						vi = (struct VIndex *)((char *)
							WS_PTR(TMP_ME) + sizeof(struct MemEnt));
					}
				}
				/*
				 * Struct arrays store ws_off_t offsets in their int32_t slots,
				 * not raw pointers.  Store WS_OFF(re) so the entry is 64-bit safe.
				 */
				*(sp.lp + m) = WS_OFF(re);
				re->ve_reference = WS_OFF(sp.lp + m);
				debug_check_ref("index.c/sp.lp", (struct MemEnt*)re, re->ve_reference); /* %%% DBG */
				re->ve_vtype &= ~VARIABLE_BIT;	/* now it's a variable */
				if (tot) {	/* need to get at it later? */
					if (vfloat == -1) {
						vfloat = AllocFloatv();
						if (vfloat == -1) return(SET_ERR(ERR_NOFLOATV));
					}
					VRIGHT = &FLOATVS[vfloat];	/* temp place to point */
					*VRIGHT = WS_OFF(re);		/* store offset, not pointer */
				}
			}
			rscan--;
			if (!rscan) {	/* reinit pointer */
				rp.cp = DATA_AD(re);
				rscan = rend;
			}
		} else {
			if (stf) {
				if (vfloat == -1) {
					vfloat = (short)AllocFloatv();
					if (vfloat == -1) return(SET_ERR(ERR_NOFLOATV));
				}
				VTEMP = &FLOATVS[vfloat];
				/*
				 * Struct data slot holds ws_off_t; retrieve it directly.
				 */
				*VTEMP = (ws_off_t)(*(sp.lp + m));
			} else {
				switch (ve->ve_vtype & BYTES_MASK) {
					case 1:
						*dp.cp++ = *(sp.cp + m);
						break;
					case 2:
						*dp.sp++ = *(sp.sp + m);
						break;
					case 4:
						*dp.lp++ = *(sp.lp + m);
						break;
					case FLOATSIZE:
						dpp = sp.dp + m;
						*dp.dp++ = *dpp;
						break;
				}
			}
		}
		tot--;
	}

	for (j=0;j<i;j++) vi[j].vp = 0L;	/* free up all tmps */

	if (!f) {
		if (stf) {
			if (!*VTEMP) return(SET_ERR(ERR_VALUE));
			/* stack the floater */
			*--MSTACK = (int32_t)(STACK_FLOATV | vfloat);
			vfloat = -1;	/* don't free it */
		} else temp_stack(WSVAL_PTR(TMP_VE));	/* stack the result */
	}
	if (vfloat != -1) FLOATVS[vfloat] = 0;		/* zero the ws_off_t slot */
	if (TMP_ME) WS_PTR(TMP_ME)->me_reference = 0;	/* mark for deletion */
	TMP_ME = 0;
	TMP_VE = 0;
	return(0);
}
