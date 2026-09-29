/*
	mplmem.c
*/

#define MPLMEM_C 1

#define DEFAULT_WSSIZE 1048576L		/* 1M default workspace */

#include "parse.h"
#include "externs.h"

#define SANITY_CHECK

int garb_debug = 1;	/* set to 1 to always garb collect for debugging */

long GarbCollect(void);


/* Note: "struct WorkSpace" is the header only, not the entire allocation.
   The full allocated block layout is:
   [struct WorkSpace header][SymTable array][MemEnt pool][stack (grows down)]
   "workspace" in the APL sense refers to the MemEnt pool only. */

struct WorkSpace *wspace = 0L;
long wssize = DEFAULT_WSSIZE;
long wsactual = 0L;

void 
debug_check_ref(const char *site, struct MemEnt *me, ws_off_t ref)
{
    /* Check if ref is outside all valid wspace ranges */
    ws_off_t symt_lo = (ws_off_t)sizeof(struct WorkSpace);
    ws_off_t symt_hi = symt_lo + (ws_off_t)(SYMTACTUAL * (int)sizeof(struct SymTable));
    ws_off_t stk_lo  = (ws_off_t)((char *)MSTACK - 100 - (char *)wspace);
    ws_off_t stk_hi  = (ws_off_t)((char *)STACKMAX - (char *)wspace);
    int ok = 0;
    if (ref >= wspace->begin && ref < wspace->water) ok = 1;
    if (!ok && ref >= 0 && ref < (ws_off_t)sizeof(struct WorkSpace)) ok = 1;
    if (!ok && ref >= symt_lo && ref < symt_hi) ok = 1;
    if (!ok && ref >= stk_lo && ref <= stk_hi) ok = 1;
    if (!ok && (me->me_flags & ME_MAL)) ok = 1;
	if (!ok) {
        char dbgbuf[128];
        NL();
        snprintf(dbgbuf, sizeof(dbgbuf),
            "BAD REF WRITE at %s: pool_off=%ld ref=%08x",
            site,
            (long)((char*)me - (char*)wspace),
            (unsigned int)ref);
        PRINT(dbgbuf);
        NL();
        abort();   /* clean stop with full backtrace in gdb */
    }
}

/*
	mem_init() -- initialise or resize the workspace.
	
	Single-allocation scheme: one malloc covers [WorkSpace header]
	[SymTable array (fixed, SYMTSIZE entries)] [MemEnt pool] ... [stack down].
	SYMT is no longer a separate allocation -- it lives at a fixed offset
	(sizeof(WorkSpace)) inside the wspace block.  SYMT_REL is gone.

	Re-initable: if wspace already exists and wssize != wsactual, the block
	is realloced/copied to the new size.  This feature is currently unused.
*/
short
mem_init(char *wsname)
{
REG short i;
ws_off_t symt_base;		/* byte offset of embedded SYMT from wspace */
ws_off_t pool_base;		/* byte offset of first MemEnt from wspace */

	symt_base = (ws_off_t)sizeof(struct WorkSpace);
	pool_base = symt_base + (ws_off_t)(SYMTSIZE * sizeof(struct SymTable));

	if (wspace)
	{
		/* Resize: copy existing content into a new allocation. */
		if (wssize != wsactual)
		{
			UCHAR *cp;

			if ((int32_t)wssize <= wspace->water) return(-1);  /* too small */
			cp = (UCHAR *)mem_alloc(wssize);
			if (!cp) return(-2);	/* not enough ram */
			memcpy((char *)cp, (char *)wspace, (size_t)wspace->water);
			free((char *)wspace);
			wspace = (struct WorkSpace *)cp;
			wspace->wssize = (int32_t)wssize;
			/* SYMT pointer recalculated from wspace base after realloc */
			SYMT = (struct SymTable *)((char *)wspace + symt_base);

			/* Stack lives at the top of the block and moved with it.
			   Callers of this resize path are required to have an empty
			   SI stack (MSTACK == STACKMAX) going in, so there's nothing
			   on the old stack that needs to survive -- just rebuild it
			   fresh against the new size, same as a fresh allocation. */
			STACKMAX = (int32_t *)(((UCHAR *)wspace) + wssize);
			*--STACKMAX = STACK_EMARK;
			MSTACK = STACKMAX;
			wsactual = wssize;
		}
	}
	else
	{
		/* Fresh allocation: workspace + embedded SYMT + pool + stack. */
		wspace = (struct WorkSpace *)mem_alloc(wssize);
		if (!wspace) return(-3);	/* not enough ram */

		/* Zero for clean debug state. */
		memset((char *)wspace, 0, (size_t)wssize);

		/* Set up stack at top of block. */
		STACKMAX = (int32_t *)(((UCHAR *)wspace) + wssize);
		*--STACKMAX = STACK_EMARK;	/* sentinel at very end */
		MSTACK = STACKMAX;			/* stack pointer starts at top */
		wsactual = wssize;

		/* Wire workspace header fields. */
		wspace->wssize  = (int32_t)wssize;
		wspace->begin   = pool_base;		/* ws_off_t offset to first MemEnt */
		wspace->water   = pool_base;		/* pool is initially empty */
		wspace->symtsize = SYMTSIZE;		/* record for save/load verification */

		/* SYMT lives immediately after WorkSpace header inside wspace. */
		SYMT = (struct SymTable *)((char *)wspace + symt_base);

		/* Call init_ws() to set origin, wsid, etc., then load default ws. */
		init_ws();
		SYMTACTUAL = SYMTSIZE;

		/* Default workspace to auto-load on a fresh start. Caller supplies
		   the name (command-line override, if any); "mpl" if none given.
		   This used to be inferred from GLOBARGV[0]'s basename (plus a
		   dead $MPL env-var prefix) -- removed because GLOBARGV/GLOBARGC
		   are now real ⎕ARGS pass-through values, not a name-encoding
		   trick, and the two can no longer share that string. */
		if (!wsname || !*wsname) wsname = "mpl";
		if (load_ws(wsname, 0))
		{

			if (DEBUGFLAG)
			{
				char *cp = (char *)LSCRATCH;
				sprintf(cp,"startup: workspace '%s' not found -- starting new",wsname);
				NL();
				PRINT(cp);
				NL();
			}
			do_clearws(0L);
		}

	}

	SYMTACTUAL = SYMTSIZE;
	for (i = 0; i < NUM_FLOATVS; i++) FLOATVS[i] = 0;	/* ws_off_t, zero = no entry */
	return(1);
}

/*
	mem_free() -- release the workspace.

	Because SYMT is embedded inside wspace, a single free() covers everything.
	The old separate SYMT free is gone.
*/
void
mem_free(void)
{
int i;

	if (wspace)
	{
		if (SYMT)
		{
			for (i = 0; i < SYMTSIZE; i++)
			{	/* orphan all symbols so GarbCollect can free ME_MAL entries */
				SYMT[i].type  = SYM_UNDEF;
				SYMT[i].value = 0;		/* ws_off_t zero = undefined */
			}
			/* force deletion of all external (ME_MAL) memory refs */
			while (GarbCollect());
		}
		free((char *)wspace);	/* single free covers wspace + SYMT + pool + stack */
	}

	/* SYMT is inside wspace -- do NOT free it separately */
	SYMT  = 0L;
	wspace = 0L;
}

/*
	MakeEnt() -- allocate a new MemEnt in the pool.

	Third arg p is now ws_off_t* -- a pointer to the ws_off_t slot that will
	own this entry (e.g. &SYMT[i].value, &fe->fe_NextLine).
	MakeEnt sets:
	    me->me_reference = WS_OFF(p)   (offset of the owning slot)
	    *p               = WS_OFF(me)  (slot now points to new entry)

	PERFORMANCE: this fn gets called lots; tuneups will be useful.
*/
struct MemEnt *
MakeEnt(long size, short typ, ws_off_t *p)
{
REG struct MemEnt *me;

	if (garb_debug) while (GarbCollect());	/* extreme garb collect when debugging */

	/* Adjust for header and round up to even byte count. */
	size += (long)sizeof(struct MemEnt);
	size += size & 1L;

	/*
	 * Space check: distance between the top of the pool and the bottom
	 * of the stack must stay above STACK_MARGIN bytes.
	 * MSTACK is int32_t*; cast both sides to (char*) for byte arithmetic,
	 * then compare as ptrdiff_t.
	 */
#define ENOUGH_SPACE() \
	(((char *)MSTACK - ((char *)wspace + wspace->water + size)) >= (long)STACK_MARGIN)

	if (!ENOUGH_SPACE())
	{
		GarbCollect();

		if (!ENOUGH_SPACE())
		{
			while (GarbCollect());

			if (!ENOUGH_SPACE()) return((struct MemEnt *)0L);	/* last gasp */
		}
	}
#undef ENOUGH_SPACE

	me = (struct MemEnt *)((char *)wspace + wspace->water);
	wspace->water += (int32_t)size;

	if (p)
	{
		*p = WS_OFF(me);			/* owning slot -> offset of new entry */
		me->me_reference = WS_OFF(p);	/* new entry knows its owner's offset */
	}
	me->me_size  = (int32_t)size;
	me->me_flags = (UCHAR)typ;
	return(me);
}

/*
	me_delete() -- mark a MemEnt as deleted and clean up its children.
	Used by GarbCollect() and sym_delete() in commands.c.

	All me_reference fields are now ws_off_t (offsets from wspace base).
	fe_NextLine / f0e_Line1 are ws_off_t; use WS_PTR() to follow them.
	FLOATVS[], TMP_ME, TMP_VE are ws_off_t.
*/
void
me_delete(struct MemEnt *me)
{
struct FuncEnt *fe, *nf;
struct ValEnt  *ve;
REG struct ValEnt *te;
union MultiP rp;
REG int32_t l;
ws_off_t me_off;

	me_off = WS_OFF(me);

	/*
	 * If the owning slot still points to us, clear it.
	 * (This case arises for CURFUNC, which is a special case where the
	 * entry points to itself.)
	 */
	if (me->me_reference)
	{
		ws_off_t *owner = (ws_off_t *)((char *)wspace + me->me_reference);
		if (*owner == me_off) *owner = 0;
	}

	switch (me->me_flags & ME_TMASK)
	{
		case ME_VAL:
			if (me->me_flags & ME_MAL)
			{
				te = (struct ValEnt *)me;	/* free external memory */
				if (te->ve_point)
					sys_mfree((char *)(intptr_t)te->ve_point, te->ve_length);
			}
			if ((me->me_vtype | VARIABLE_BIT) == STRUCT_CODE)
			{
				/*
				 * A struct value contains a series of ws_off_t links to
				 * child ValEnts.  Walk them and recursively delete any
				 * child that still considers us its owner.
				 */
				ve  = (struct ValEnt *)me;
				rp.cp = DATA_AD(ve);
				l = ve->ve_length;
				while (l--)
				{
					ws_off_t child_off = *rp.lp;
					if (child_off)
					{
						te = (struct ValEnt *)WS_PTR(child_off);
						if (te->ve_reference == me_off)
						{
							me_delete((struct MemEnt *)te);
						}
#ifdef SANITY_CHECK
						else
						{
							NL();
							PRINT("me_delete: Sync error");
							NL();
						}
#endif
						*rp.lp++ = 0;
					}
					else
					{
						rp.lp++;
					}
				}
			}
			break;

		case ME_FUNC0:
			/*
			 * Walk the function-line chain and delete each line that
			 * still considers the previous entry its owner.
			 */
			fe = (struct FuncEnt *)me;
			while (fe)
			{
				ws_off_t next_off = fe->fe_NextLine;
				if (!next_off) break;
				nf = (struct FuncEnt *)WS_PTR(next_off);
				/* Check that nf's owner slot is &fe->fe_NextLine */
				if (nf->fe_MemEnt.me_reference == WS_OFF(&fe->fe_NextLine))
				{
					fe->fe_NextLine = 0;
					fe = nf;
				}
				else
				{
#ifdef SANITY_CHECK
					NL();
					PRINT("me_delete: Sync2 error");
					NL();
#endif
					break;
				}
			}
			break;

		case ME_FUNCN:
			fe = (struct FuncEnt *)me;
			if (fe->fe_NextLine)
			{
				nf = (struct FuncEnt *)WS_PTR(fe->fe_NextLine);
				if (nf->fe_MemEnt.me_reference == WS_OFF(&fe->fe_NextLine))
					fe->fe_NextLine = 0;
			}
			break;

		case ME_NAM:
			break;

		case ME_ITMP:
			l = me->me_size - (int32_t)sizeof(struct MemEnt);
			memset((char *)me + sizeof(struct MemEnt), 0, (size_t)l);
			break;
	}
	me->me_reference = 0;	/* ws_off_t zero = no owner */
}

/*
	GarbCollect() -- compact the MemEnt pool, returns bytes recovered.

	All pointer/offset arithmetic is now in terms of ws_off_t.
	SANITY_CHECK is a flat range test (no 7-level nesting).
	FLOATVS[], TMP_ME, TMP_VE adjustments use offset arithmetic.
	fe_NextLine / children's me_reference adjustments use offset arithmetic.
*/
long
GarbCollect(void)
{
int32_t adj;
int32_t s;
int32_t l;
int32_t *lp;
REG int i;
REG struct MemEnt *me;
struct MemEnt *md, *te;
struct FuncEnt *fe;
struct ValEnt  *ve;

	/* Sentinel: one past the last live byte in the pool. */
	md  = (struct MemEnt *)((char *)wspace + wspace->water);
	adj = 0;
	me  = (struct MemEnt *)((char *)wspace + wspace->begin);

	/* Walk every entry in the pool. */
	while (me < md)
	{
	ws_off_t me_ref = me->me_reference;
	int me_is_live = 0;

		if (me_ref)
		{
			me_is_live = (me == (struct MemEnt *)((char *)wspace +
				*(ws_off_t *)((char *)wspace + me_ref)));
		}

#ifdef SANITY_CHECK
		/*
		 * Flat range check replaces 7-level nesting.
		 * me_reference is a ws_off_t; valid ranges within wspace:
		 *   Pool range:  [wspace->begin, wspace->water)
		 *   SYMT range:  [sizeof(WorkSpace), sizeof(WorkSpace) + SYMTACTUAL*sizeof(SymTable))
		 *   Header range:[0, sizeof(WorkSpace))  -- covers curfunc, tmp_me, tmp_ve fields
		 *   Stack range: [WS_OFF(MSTACK)-100, WS_OFF(STACKMAX)]  (roughly)
		 * curfunc/tmp_me/tmp_ve are now fields inside WorkSpace header (offset < sizeof(WorkSpace)),
		 * so no special-case check is needed for them.
		 */
		if (me_ref)
		{
			ws_off_t ref = me_ref;
			int ref_ok = 0;

			/* Pool range */
			if (ref >= wspace->begin && ref < wspace->water)
				ref_ok = 1;
			/* WorkSpace header range */
			if (!ref_ok && ref >= 0 && ref < (ws_off_t)sizeof(struct WorkSpace))
				ref_ok = 1;
			/* SYMT range */
			if (!ref_ok)
			{
				ws_off_t symt_lo = (ws_off_t)sizeof(struct WorkSpace);
				ws_off_t symt_hi = symt_lo +
					(ws_off_t)(SYMTACTUAL * (int)sizeof(struct SymTable));
				if (ref >= symt_lo && ref < symt_hi) ref_ok = 1;
			}
			/* Stack range */
			if (!ref_ok)
			{
				ws_off_t stk_lo = (ws_off_t)((char *)MSTACK - 100 - (char *)wspace);
				ws_off_t stk_hi = (ws_off_t)((char *)STACKMAX - (char *)wspace);
				if (ref >= stk_lo && ref <= stk_hi) ref_ok = 1;
			}
			/* ME_MAL entries */
			if (!ref_ok && (me->me_flags & ME_MAL)) ref_ok = 1;

			if (!ref_ok && me_is_live)
			{
				char dbgbuf[128];
				NL();
				snprintf(dbgbuf, sizeof(dbgbuf),
					"GarbCollect(): reference out of range: off=%08lx MSTACK off=%08lx",
					(long)me_ref,
					(long)((char *)MSTACK - (char *)wspace));
				PRINT(dbgbuf);
				NL();
			}
		}
#endif /* SANITY_CHECK */

		s = me->me_size;	/* size of this entry (bytes) */

		if (!me_ref || !me_is_live)
		{
			/* Garbage: owning slot no longer points back to us. */
			adj += s;
			if (me_ref) me_delete(me);
		}
		else
		{
			if (adj)
			{
				/*
				 * This live entry is moving down by adj bytes.
				 * Three things must be updated:
				 *   1. FLOATVS[] trackers that match this entry
				 *   2. The owning slot (me_reference target)
				 *   3. Children's me_reference fields
				 */

				/* 1. Adjust floating trackers. */
				for (i = 0; i < NUM_FLOATVS; i++)
				{
					if (FLOATVS[i] == WS_OFF(me))
						FLOATVS[i] -= adj;
				}

				/* 2. Adjust the owning slot: it stored WS_OFF(me), now -= adj. */
				if (me->me_reference)
				{
					ws_off_t *owner =
						(ws_off_t *)((char *)wspace + me->me_reference);
					*owner -= adj;
				}

				/* 3. Adjust children's back-references. */
				i = me->me_flags & ME_TMASK;
				if (i == ME_FUNC0 || i == ME_FUNCN)
				{
					fe = (struct FuncEnt *)me;
					if (fe->fe_NextLine)
					{
						te = WS_PTR(fe->fe_NextLine);
						te->me_reference -= adj;
					}
				}
				else if (i == ME_VAL)
				{
					ve = (struct ValEnt *)me;
					if ((ve->ve_vtype | 0x80) == STRUCT_CODE)
					{
						l  = DATA_LEN(ve);
						lp = (int32_t *)DATA_AD(ve);
						/*
						 * Each slot holds a ws_off_t to a child ValEnt.
						 * Adjust the child's me_reference, not the slot
						 * itself (the child is not moving right now).
						 */
						while (l--)
						{
							ws_off_t child_off = (ws_off_t)*lp++;
							if (child_off)
							{
								te = WS_PTR(child_off);
								te->me_reference -= adj;
							}
						}
					}
				}

				/*
				 * Must move incrementally: pointers may need adjusting twice
				 * (once when the parent moves, once when the child moves).
				 * memmove, not memcpy: whenever the entry is larger than the
				 * gap it's closing (s > adj), source and destination overlap,
				 * which memcpy doesn't allow.
				 */
				memmove((char *)me - adj, (char *)me, (size_t)s);
			}
		}

		me = (struct MemEnt *)((char *)me + s);
	}

	wspace->water -= adj;

	if (md != (struct MemEnt *)((char *)wspace + wspace->water + adj))
	{
		/* System error: watermark doesn't match entry traversal. */
		message(SET_ERR(ERR_SYSTEM));
		NL();
		PRINT("NAB pointer out of sync");
		NL();
		do_si(0);
		die(1);
	}

	return((long)adj);
}

int
AllocFloatv(void)
{
REG int i;

	for (i = 0; i < NUM_FLOATVS; i++)
	{
		if (!FLOATVS[i]) return(i);
	}
	return(-1);
}
