/*
 * stub_frontend.c -- minimal headless frontend for APL backend testing
 *
 * Implements the PlatformFuncs interface (platform.h) using stdio only.
 * Links with libapl.a to give a runnable interpreter without Qt.
 *
 * Build (once Makefile exists):
 *   gcc -DNO_INPUT_PROC -o stub_test stub_frontend.c -L. -lapl
 *
 * Or manually during Phase 2 (with some .o files still missing/stubbed):
 *   gcc -DNO_INPUT_PROC -o stub_test stub_frontend.c mplmem.o commands.o \
 *       fn.o parse.o vprint.o exec.o index.o globals.o terminal.o msgs.o \
 *       linux_platform.o lineproc.o [other .o files as available]
 *
 * Usage:
 *   ./stub_test                  -- interactive session
 *   ./stub_test < script.apl     -- batch mode from file
 *
 * Notes:
 *   - NO_INPUT_PROC must be defined so lineproc.c uses alt_getin()
 *     from linux_platform.c rather than the raw terminal path.
 *   - No APL font here; APL chars appear as their byte values.
 *     Acceptable for backend logic testing.
 *   - SIGINT sets ABORTSIG; the backend polls it in long-running loops.
 *   - Raw/cook mode is a no-op (stdio handles it).
 *   - Cursor movement ignored (line-oriented output is fine for testing).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "parse.h"
#include "platform.h"

extern void lp_setabort(void);

/* raw/cook/abort: use the real termios implementations from linux_platform.c */
extern void lp_setraw(void);
extern void lp_setcook(void);
extern void lp_setabort(void);

static void stub_open(void)
{
    /* terminal.c calls this via platform_open_fn on live() */
    lp_setabort();   /* arm SIGINT handler */
}

static void stub_close(void)
{
    lp_setcook();    /* restore terminal on exit */
}

/* ------------------------------------------------------------------ */
/*  Signal handler                                                     */
/* ------------------------------------------------------------------ */

static void stub_sigint(int sig)
{
    (void)sig;
    ABORTSIG = 1;
}

/* ------------------------------------------------------------------ */
/*  PlatformFuncs implementations                                      */
/* ------------------------------------------------------------------ */

static void stub_puts(const char *s)
{
    fputs(s, stdout);
}

/* pchar_fn signature: int (*)(char c) -- returns the char, like putchar */
static int stub_pchar(char c)
{
    return putchar((unsigned char)c);
}

static void stub_nl(void)
{
    putchar('\n');
}

/* cursor_move_fn signature: void (*)(short x, short y) */
static void stub_cursor_move(short x, short y)
{
    (void)x; (void)y;
}

static void stub_cursor_left(short n)  { (void)n; }
static void stub_cursor_right(short n) { (void)n; }
static void stub_cursor_up(short n)    { (void)n; }
static void stub_cursor_down(short n)  { (void)n; }

static void stub_clear_eol(void)    { /* no-op for line-oriented stub */ }
static void stub_clear_screen(void) { /* no-op */ }

/* standout: could emit ANSI escapes but left as no-op for clean output */
static void stub_standout(int on) { (void)on; }

/* ------------------------------------------------------------------ */
/*  Registration struct                                                */
/*                                                                     */
/*  Field names and order must match PlatformFuncs in platform.h.     */
/* ------------------------------------------------------------------ */

static const PlatformFuncs stub_funcs = {
    .puts_fn          = stub_puts,
    .pchar_fn         = stub_pchar,
    .nl_fn            = stub_nl,
    .cursor_move_fn   = stub_cursor_move,
    .cursor_left_fn   = stub_cursor_left,
    .cursor_right_fn  = stub_cursor_right,
    .cursor_up_fn     = stub_cursor_up,
    .cursor_down_fn   = stub_cursor_down,
    .clear_eol_fn     = stub_clear_eol,
    .clear_screen_fn  = stub_clear_screen,
    .standout_fn      = stub_standout,
    .open_fn          = stub_open,
    .close_fn         = stub_close,
    .setraw_fn        = lp_setraw,
    .setcook_fn       = lp_setcook,
    .setabort_fn      = lp_setabort,
};

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

/* apl_main() is void -- it runs the interpreter loop and calls die()
 * to exit.  Declared extern here; defined in main.c. */
extern void apl_main(int argc, char **argv);

int 
main(int argc, char **argv)
{
    /* Arm SIGINT before calling into the backend */
    signal(SIGINT, stub_sigint);

    /* Register the stdio platform before platform_defaults() or
     * mem_init() so that any early PRINT/NL calls route correctly. */
    platform_register(&stub_funcs);

    fprintf(stdout, "[stub_frontend: headless APL test mode]\n");
    fflush(stdout);

    apl_main(argc, argv);   /* does not return; calls die() */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Stub bodies for not-yet-ported files                              */
/*                                                                     */
/*  If a .c file hasn't been fixed yet and prevents linking, add a   */
/*  temporary stub body here and compile with -DSTUB_FOO.            */
/*  Remove each stub as the real file lands.                          */
/*                                                                     */
/*  Example:                                                           */
/*  #ifdef STUB_SORT                                                   */
/*  void do_sort(void) { fprintf(stderr,"STUB: sort\n"); }            */
/*  #endif                                                             */
/* ------------------------------------------------------------------ */
