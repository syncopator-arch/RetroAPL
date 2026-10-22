// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
    terminal.c
    Common terminal output -- platform-independent backend.

    All actual output is routed through function pointers registered
    by the Qt frontend via platform_register() at startup.
    Curses/termcap dependencies have been removed entirely.

    Output buffering (V22):
    sys_puts() and sys_pchar() accumulate text in s_outbuf rather than
    posting a Qt invokeMethod call per character/string.  NL() flushes
    the buffer then posts the newline -- one cross-thread call per output
    line instead of potentially thousands.  flush_output() force-flushes
    any partial line with no trailing newline; it is called by exec_apl_line()
    in main.c before returning so that error messages and single-line results
    always appear promptly.  This also keeps the Qt event loop responsive
    enough to process Ctrl+C (ABORTSIG) during continuous output.
*/

#define TERMINAL_C 1

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "parse.h"
#include "mplmem.h"
#include "platform.h"
#include "externs.h"

/* ------------------------------------------------------------------ */
/*  Output buffer                                                       */
/*  Sized to hold one full-width line plus a little headroom.          */
/*  Content is always null-terminated so platform_puts_fn can use it  */
/*  as a plain C string.                                               */
/* ------------------------------------------------------------------ */

#define OUTBUF_SIZE 512

static char s_outbuf[OUTBUF_SIZE];
static int  s_outlen = 0;

/* ------------------------------------------------------------------ */
/*  Platform function pointer table                                    */
/* ------------------------------------------------------------------ */

void (*platform_puts_fn)(const char *s)         = 0;
int  (*platform_pchar_fn)(char c)               = 0;
void (*platform_nl_fn)(void)                    = 0;
void (*platform_cursor_move_fn)(short x,short y)= 0;
void (*platform_cursor_left_fn)(short n)        = 0;
void (*platform_cursor_right_fn)(short n)       = 0;
void (*platform_cursor_up_fn)(short n)          = 0;
void (*platform_cursor_down_fn)(short n)        = 0;
void (*platform_clear_eol_fn)(void)             = 0;
void (*platform_clear_screen_fn)(void)          = 0;
void (*platform_home_fn)(void) 					= 0;
void (*platform_standout_fn)(int on)            = 0;
void (*platform_open_fn)(void)                  = 0;
void (*platform_close_fn)(void)                 = 0;
void (*platform_setraw_fn)(void)                = 0;
void (*platform_setcook_fn)(void)               = 0;
void (*platform_setabort_fn)(void)              = 0;

void
platform_register(const PlatformFuncs *fns)
{
    platform_puts_fn        = fns->puts_fn;
    platform_pchar_fn       = fns->pchar_fn;
    platform_nl_fn          = fns->nl_fn;
    platform_cursor_move_fn = fns->cursor_move_fn;
    platform_cursor_left_fn = fns->cursor_left_fn;
    platform_cursor_right_fn= fns->cursor_right_fn;
    platform_cursor_up_fn   = fns->cursor_up_fn;
    platform_cursor_down_fn = fns->cursor_down_fn;
    platform_clear_eol_fn   = fns->clear_eol_fn;
    platform_clear_screen_fn= fns->clear_screen_fn;
	platform_home_fn 		= fns->home_fn;
    platform_standout_fn    = fns->standout_fn;
    platform_open_fn        = fns->open_fn;
    platform_close_fn       = fns->close_fn;
    platform_setraw_fn      = fns->setraw_fn;
    platform_setcook_fn     = fns->setcook_fn;
    platform_setabort_fn    = fns->setabort_fn;
    platform_edit_fn        = fns->edit_fn;
	platform_getin_fn 		= fns->getin_fn;
}

/* ------------------------------------------------------------------ */
/*  flush_output -- push buffered text to the frontend.               */
/*  Called by NL() before the newline, and by exec_apl_line() on      */
/*  return to catch any trailing output that has no newline.           */
/*                                                                     */
/*  Crash-log transcript (crashlog.c): everything that reaches the     */
/*  frontend is recorded on the way -- here, in sys_puts()'s long-     */
/*  string path, and a newline in NL()/sys_nl(). Those are the only    */
/*  places text leaves this file (sys_pchar() buffers; nothing here    */
/*  calls platform_pchar_fn).                                          */
/* ------------------------------------------------------------------ */

void
flush_output(void)
{
    if (s_outlen > 0 && platform_puts_fn) {
        s_outbuf[s_outlen] = '\0';
        crash_record_output(s_outbuf, s_outlen);
        platform_puts_fn(s_outbuf);
        s_outlen = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Convenience wrappers -- safe to call even before registration     */
/* ------------------------------------------------------------------ */

void 
sys_puts(const char *s)
{
    const char *p, *seg;
    int seglen;
    if (!s || !platform_puts_fn) return;

    seg = s;

    for (p = s; *p; p++) {
		/* the empty loop is needed, but this wasn't the right place for this, it's in vout() */
    }

    /* remainder after last 0x01 (or whole string if none) */
    seglen = (int)(p - seg);
    if (seglen <= 0) return;
    if (s_outlen + seglen >= OUTBUF_SIZE - 1)
        flush_output();
    if (seglen >= OUTBUF_SIZE - 1) {
        crash_record_output(seg, seglen);
        platform_puts_fn(seg);
    } else {
        memcpy(s_outbuf + s_outlen, seg, seglen);
        s_outlen += seglen;
    }
    CURSX += (short)seglen;
}

int 
sys_pchar(char c)
{
    if (!platform_puts_fn) return (int)(unsigned char)c;

    if (s_outlen >= OUTBUF_SIZE - 1)
        flush_output();

    s_outbuf[s_outlen++] = c;
    CURSX++;
    return (int)(unsigned char)c;
}

void sys_nl(void)
{
    /* sys_nl() is retained for any direct callers but routes through NL() logic */
    flush_output();
    crash_record_nl();
    if (platform_nl_fn) platform_nl_fn();
}

void platform_open(void)
{
    if (platform_open_fn) platform_open_fn();
}

void platform_close(void)
{
    if (platform_close_fn) platform_close_fn();
}

void setraw(void)
{
    if (platform_setraw_fn) platform_setraw_fn();
}

void setcook(void)
{
    if (platform_setcook_fn) platform_setcook_fn();
}

void setabort(void)
{
    if (platform_setabort_fn) platform_setabort_fn();
}

void platform_clear_eol(void)
{
    if (platform_clear_eol_fn) platform_clear_eol_fn();
}

void platform_clear_screen(void)
{
    if (platform_clear_screen_fn) platform_clear_screen_fn();
}

void platform_home(void)
{
    if (platform_home_fn) platform_home_fn();
}

void platform_standout(int on)
{
    if (platform_standout_fn) platform_standout_fn(on);
}

/* ------------------------------------------------------------------ */
/*  Cursor movement -- update internal position tracking and          */
/*  notify frontend.  Qt manages its own display; these keep the      */
/*  backend's CURSX/CURSY state consistent for any code that reads    */
/*  them (e.g. ledfuncs.c line wrapping logic).                       */
/* ------------------------------------------------------------------ */

void
gcursor_move(short x, short y)
{
    CURSX = x;
    CURSY = y;
    if (platform_cursor_move_fn) platform_cursor_move_fn(x, y);
}

void
gcursor_left(short n)
{
    CURSX -= n;
    if (CURSX < 0) CURSX = 0;
    gcursor_move(CURSX, CURSY);
}

void
gcursor_right(short n)
{
    CURSX += n;
    if (CURSX > SCRW) CURSX = SCRW;
    gcursor_move(CURSX, CURSY);
}

void
gcursor_up(short n)
{
    CURSY -= n;
    if (CURSY < FONTH) CURSY = FONTH - 1;
    gcursor_move(CURSX, CURSY);
}

void
gcursor_down(short n)
{
    CURSY += n;
    while (CURSY > SCRH) CURSY -= FONTH;
    gcursor_move(CURSX, CURSY);
}

/* ------------------------------------------------------------------ */
/*  NL -- newline.  Flushes the output buffer first so the complete   */
/*  line reaches the frontend in a single cross-thread call, then     */
/*  notifies the frontend of the newline and resets CURSX.            */
/* ------------------------------------------------------------------ */

void
NL(void)
{
    flush_output();
    crash_record_nl();
    if (platform_nl_fn) platform_nl_fn();
    CURSX = 0;
    /* Qt frontend handles actual scrolling and display */
}

/* ------------------------------------------------------------------ */
/*  sys_print -- print a string, turning off graphics mode first      */
/* ------------------------------------------------------------------ */

void
sys_print(unsigned char *s)
{
    sys_puts((const char *)s);
}

/* ------------------------------------------------------------------ */
/*  m_putp -- was tputs() for curses capability strings.              */
/*  Now a no-op: Qt handles all terminal control.                     */
/*  Kept so callers in ledfuncs.c etc. still compile.                 */
/* ------------------------------------------------------------------ */

void
m_putp(char *s)
{
    (void)s;    /* no-op -- Qt handles terminal control */
}

/* ------------------------------------------------------------------ */
/*  open_terminal -- was curses setupterm().                          */
/*  Now just returns success; Qt frontend does its own init.          */
/* ------------------------------------------------------------------ */

int
open_terminal(void)
{
    return(1);
}

/* ------------------------------------------------------------------ */
/*  valid_cpp -- legacy stub, returns 1                               */
/* ------------------------------------------------------------------ */

int
valid_cpp(void)
{
    return(1);
}
