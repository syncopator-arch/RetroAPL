/*
	mplmem.h
*/

/*
 * SYMT, SYMTSIZE, SYMTACTUAL, MSTACK, STACKMAX are all covered by the
 * macros in globals.h -- do NOT declare them here.
 *
 * wspace is the single allocation that contains the WorkSpace header,
 * the embedded SymTable array, and the MemEnt pool.  wssize and wsactual
 * are the allocated size and the current high-water size respectively.
 */

extern struct WorkSpace *wspace;
extern long wssize;
extern long wsactual;

extern short mem_init(char *wsname);
extern long  GarbCollect(void);

/*
 * MakeEnt() third arg is now ws_off_t* (was struct MemEnt **).
 * It receives a pointer to the ws_off_t slot that will own the new entry
 * (e.g. &SYMT[i].value, &fe->fe_NextLine).  MakeEnt sets
 * new_entry->me_reference = WS_OFF(p) and *p = WS_OFF(new_entry).
 */
extern struct MemEnt *MakeEnt(long size, short typ, ws_off_t *p);

/* %%% debug only */
extern void debug_check_ref(const char *site, struct MemEnt *me, ws_off_t ref);
