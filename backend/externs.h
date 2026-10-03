/*
	externs.h
*/

#include <stddef.h>	/* size_t -- needed by get_aplhome_dir() below.
			   externs.h has no includes of its own otherwise;
			   this is deliberately the one addition, rather
			   than assuming size_t is already visible from
			   whatever a given including .c file happened to
			   pull in first. */

extern UCHAR *all_fnames[];
extern int term_fd;

extern short upsizef();
extern short upsize();

extern PTF all_funcs[];
extern UCHAR *all_ftypes[];
extern UCHAR *get_vstr();
extern UCHAR char_flip();
extern UCHAR file_fnames[];
extern UCHAR file_ftypes[];
extern UCHAR op_table[];
extern UCHAR sys_fnames[];
extern UCHAR sys_ftypes[];
extern UCHAR var_lo,var_hi,alph_lo,alph_hi;
extern char *get_first_dir();
extern char *get_next_dir();
/* extern char *getenv(); */
extern char *sys_malloc();
extern char cectable[];
extern char ceitable[];
extern long GarbCollect();
extern long compsize();
extern long file_serial();
extern int32_t get_tod();
extern long wssize;
extern short abortsig;
extern short compline();
#ifndef MONOP_C
extern short do_dhrot();
extern short do_ohrot(void);
extern short do_ovrot(void);
#endif
extern short do_depsilon(void);
extern short do_dyop();
extern short do_exec();
extern short do_index();
#ifndef MONOP_C
extern short do_monop();
#endif
extern short file_funcs();
extern short getach_e();
extern short getdlist();
extern short guru_abort;
extern short mem_init(char *wsname);
extern short outsync;
extern short pop_fn();
extern short restart_mode;
extern short stack_pop();
extern short lookup_sym();
extern short sys_funcs();
extern struct FuncEnt *MakeFNEnt();
extern ws_off_t *get_vparm();
extern struct ValEnt *dup_val();
extern struct ValEnt *make_temp();
extern unsigned long myrnd(unsigned long n);
/* largest n myrnd() accepts (rndsrc.c's RL_SPAN, the generator's number
   of distinct states, 2^31-2); callers that must check up front (deal)
   use this rather than repeating the number */
#define RND_MAXN 2147483646L
extern unsigned long rndcore();
/* ⎕RL (sysfuncs.c case 6): the generator state in rndsrc.c. rl_set()
   returns 0, changing nothing, if s is out of range. */
extern int32_t rl_get(void);
extern short rl_set(long s);
extern void Adjust_Stack();
extern void get_mplenv(char *s);
extern void message(short n);
extern unsigned char NEGSIGN;
extern void go_lower(char *cp);

#ifndef COMMANDS_C
extern short do_enter();
extern short do_command(UCHAR *line);
extern short print_name(short i);
extern void caseflip(UCHAR *cp);
extern void init_ws();
extern short do_clearws(long a);
extern short load_ws(char *s,int f);
extern short do_si(short f);
/* do_si()'s walk with a cap: newest frames from the top, oldest from the
   bottom, a count of the rest; newest < 0 means no cap (do_si() itself).
   Used by the crash log. */
extern short si_walk(short f, long newest, long oldest);
#endif

#ifndef DYOP_C
extern short do_dtrans();
extern short do_ocat(void);
/* shared with inprod.c: do_iprod()'s Stage 5b pairwise (g) step reuses
   this exact per-element dispatch instead of duplicating it.
   accp/acc2p are int64_t*, matching the definition since V50; this
   prototype still said long* until V73 -- the same type on Linux, but
   32 bits on Windows, so it agreed with inprod.c's old long variables
   and kept the compiler from flagging the mismatch there. */
extern short apply_scalar_dyop(UCHAR c, short csize, short i, short t, int64_t *accp, int64_t *acc2p, double *daccp, double *dacc2p);
#endif
/* float -> int32_t without undefined behavior (dyop.c): 1 and *out set if
   d fits (truncated toward zero), 0 if out of range or NaN -- the caller
   picks the error. Used wherever a float is read as a count, index or
   axis (V74). Outside the DYOP_C guard on purpose: dyop.c itself uses it
   above its definition, and a matching prototype can't conflict. */
extern short dbl_to_i32(double d, int32_t *out);

#ifndef EXEC_C
extern void push_valent(struct ValEnt *ve);
extern short exec_lineb();
extern void lexec();
extern void prof_dump();
extern void prof_clear();
extern void temp_stack(struct ValEnt *);
extern void swapdrop();
extern void temp_stack(struct ValEnt *te);
extern void exec_setup(UCHAR *src,short ilb);
extern void debug_report();
#endif

#ifndef FN_C
extern short pop_fn();
extern void state_stack(short fnum,int locvars);
extern void begin_fn(short fnum);
extern void end_fn();
extern void error_push();
extern short global_save(short r);
extern short global_restore(short r);
#endif

#ifndef INTERFAC_C
extern void die(short f);
extern void live();
extern void proc_abort(int n);
#endif

#ifndef LINEPROC_C
extern short getin(UCHAR *b);
#endif

#ifndef MAIN_C
extern void cdump(UCHAR *s);
extern int exec_nonfn_line(UCHAR *line);
/* Internal inconsistency: write crash.log, show msg, exit 70. Never returns. */
extern void fatal_error(const char *msg);
#endif

#ifndef CRASHLOG_C
/* crashlog.c -- $APLHOME/crash.log and the in-memory session transcript
   it includes. Recorders: terminal.c (output), platform_translate.c and
   exec.c (input lines). crash_log_write() is fatal_error()'s; the front
   ends use crash_set_transcript(), crash_log_pending() and
   crash_log_acknowledge(). */
extern void crash_set_transcript(int on);
extern void crash_record_output(const char *s, int len);
extern void crash_record_nl(void);
extern void crash_record_input(const UCHAR *line);
extern int  crash_log_write(const char *msg, char *path, size_t pathsz);
/* a warning entry: survivable inconsistency, the session carries on */
extern int  crash_log_note(const char *msg, char *path, size_t pathsz);
/* 0 nothing pending, 1 an unacknowledged crash, 2 only warnings */
extern int  crash_log_pending(char *path, size_t pathsz, char *summary, size_t sumsz);
extern int  crash_log_acknowledge(void);
#endif

#ifndef MONOP_C
extern short do_dvrot(short f);
/* shared with sysfuncs.c: ⎕NTA/⎕X quad functions call these same helpers
   that back the OP_FMT/OP_QX primitives, see monop.c */
extern short fmt_number(struct ValEnt *re, char *cp);
extern short prep_exec_line(struct ValEnt *re);
/* shared with inprod.c: do_iprod()'s f-fold uses the same identity-element
   and pairwise-combine logic that backs f/ reduction, see monop.c */
extern short reduce_identity(short op, UCHAR dtype, int32_t *acc, double *dacc);
extern short reduce_combine(short op, short sdir, UCHAR dtype, int32_t *acc, int32_t acc2, double *dacc, double dacc2);
#endif

#ifndef MPLMEM_C
extern int garb_debug;
extern void me_delete(struct MemEnt *me);
extern int AllocFloatv();
extern void mem_free();
#endif

/* platform functions -- implemented in msdos.c replacement for Linux */
extern int check_for_interrupt(int f);
extern UCHAR untrans_char(UCHAR c);
extern UCHAR unktrans_char(UCHAR c);
extern char *perform_cd(char *s);
extern void _clearscreen(int n);
extern int calledit(char *s);
extern short OUT_TRANS_FLAG;
extern short FE_TRANS_FLAG;
extern short symbolcase;
extern void set_width(int n);
extern void sys_nl();
extern void sys_puts(const char *s);
extern int sys_pchar(char c);
extern void sys_mfree(char *s,long l);
extern void long_swap(char *cp);
extern void short_swap(char *cp);
extern void calldir(char *s);
extern short callsystem(char *s);
extern int waitsystem(char *s);
extern void handle_line_event(int f);
extern void dump_output();
extern void cleanup_dir();
extern short sys_fdelete(char *s);
extern void setcook();
extern void platform_close();
extern void platform_open();
extern void setabort();
extern void setraw();
extern void time_delay(long t);
/* CTRL, mode ⎕CTRL. Minimize/restore/maximize the main terminal
   window -- mode 0 = minimize, 1 = restore, 2 = maximize. Implemented
   in main.cpp (Qt6, single implementation for every platform Qt6
   targets here). Reintroduces the name of the old, removed,
   Windows-only sys_ctrl() -- this is a full replacement, not a
   revival of that implementation. */
extern void sys_ctrl(long mode);
/* $APLHOME/<subdir>/ resolution (creates the directory if it doesn't
   already exist). Falls back to $HOME/apl/<subdir>/, then ./<subdir>/,
   if $APLHOME isn't set. Used by WILOAD (grfuncs.c) to resolve a
   non-absolute filename against $APLHOME/images/ -- same convention
   as the existing ws/fns/bin subdirectories, not a new mechanism. */
extern void get_aplhome_dir(char *out, size_t outsz, const char *subdir);
extern unsigned int platform_get_seed(void);	/* rndsrc.c's starting seed */

#ifndef PARSE_C
extern short parsenums(UCHAR **cpip,short *len);
#endif

#ifndef SORT_C
void heapsort(long n,char *rap,char *rbp);
#endif

#ifndef SYS_C
extern short do_sys(short f);
short push_val(short c,short r,int32_t *l);
#endif

#ifndef TERMINAL_C
extern void NL();
extern void sys_print(unsigned char *s);
extern void gcursor_move(short x,short y);
extern void gcursor_left(short n);
extern void gcursor_right(short n);
extern void gcursor_up(short n);
extern void gcursor_down(short n);
extern void m_putp(char *s);
#endif

#ifndef VPRINT_C
extern void convert_numstring(unsigned char *pp,short w);
extern void vprint(struct MemEnt *me);
extern void vout(unsigned char *s,short f,int ln);
#endif

/* .fn file translation for )ENTER and proc_quad */
extern int file_translate(UCHAR *cp, UCHAR *outs);
extern int alt_input_proc(UCHAR *cp, UCHAR *outs);

/* Translation tables */
extern const UCHAR orig_trans[256];
extern void translate_init(UCHAR *kt);

/* )FONT's font-family source. Link-time hook, same arrangement as
   sysfuncs.c's sys_ctrl(): the Qt build defines the real one in main.cpp,
   apl_cli defines a placeholder that returns -1. Fills
   buf with NUL-terminated names back to back, whole names only, and
   returns how many it wrote; -1 means no font list in this build.
   Declared here rather than in externs.h, following get_aplhome_dir()'s
   local declaration just above -- externs.h wasn't available to edit. */
extern long sys_font_families(char *buf, long bufsize);


