// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
    platform.h

    Interface between the APL backend and the platform-specific frontend.

    The Qt frontend calls platform_register() at startup to supply
    implementations of the output and control functions the backend needs.
    The backend never calls Qt directly -- it calls through these pointers.

    Interrupt handling: the Qt frontend sets ABORTSIG directly when the
    user presses the interrupt key (Attn equivalent).  The backend polls
    ABORTSIG in long-running loops and in vprint().  No signal machinery
    needed -- direct shared global since backend and frontend are statically
    linked into the same process.
*/

#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Output function pointers -- set by platform_register()            */
/* ------------------------------------------------------------------ */

/* Print a null-terminated string */
extern void (*platform_puts_fn)(const char *s);

/* Print a single character, returns the character (for tputs compat) */
extern int  (*platform_pchar_fn)(char c);

/* Emit a newline / advance to next line */
extern void (*platform_nl_fn)(void);

/* Cursor control -- may be no-ops initially; Qt manages its own display */
extern void (*platform_cursor_move_fn)(short x, short y);
extern void (*platform_cursor_left_fn)(short n);
extern void (*platform_cursor_right_fn)(short n);
extern void (*platform_cursor_up_fn)(short n);
extern void (*platform_cursor_down_fn)(short n);
extern void (*platform_clear_eol_fn)(void);
extern void (*platform_clear_screen_fn)(void);
extern void (*platform_home_fn)(void);

/* Standout (reverse video) attribute -- no-op initially */
extern void (*platform_standout_fn)(int on);   /* 1 = on, 0 = off */

/* Platform lifecycle -- called by die() and live() */
extern void (*platform_open_fn)(void);
extern void (*platform_close_fn)(void);

/* Set terminal raw/cooked mode -- no-ops with Qt */
extern void (*platform_setraw_fn)(void);
extern void (*platform_setcook_fn)(void);
extern void (*platform_setabort_fn)(void);

/* Qt editor -- called by )EDIT command; blocks until editor exits */
extern void (*platform_edit_fn)(const char *fname);

/* quad alt input fn */
extern short (*platform_getin_fn)(UCHAR *p);

/* fn file character translation logic */
extern int file_translate(unsigned char *src, unsigned char *dst);
extern int alt_input_proc(unsigned char *cp, unsigned char *outs);
extern int alt_getin(char *b);

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    void (*puts_fn)(const char *s);
    int  (*pchar_fn)(char c);
    void (*nl_fn)(void);
    void (*cursor_move_fn)(short x, short y);
    void (*cursor_left_fn)(short n);
    void (*cursor_right_fn)(short n);
    void (*cursor_up_fn)(short n);
    void (*cursor_down_fn)(short n);
    void (*clear_eol_fn)(void);
    void (*clear_screen_fn)(void);
	void (*home_fn)(void);       /* 0x02 — home cursor to top-left, no clear */
    void (*standout_fn)(int on);
    void (*open_fn)(void);
    void (*close_fn)(void);
    void (*setraw_fn)(void);
    void (*setcook_fn)(void);
    void (*setabort_fn)(void);
    void (*edit_fn)(const char *fname);   /* Qt editor handshake */
	short (*getin_fn)(UCHAR *p);
} PlatformFuncs;

void platform_register(const PlatformFuncs *fns);
void platform_defaults(void);

/* ------------------------------------------------------------------ */
/*  Convenience wrappers -- backend code calls these                  */
/* ------------------------------------------------------------------ */

void sys_puts(const char *s);
int  sys_pchar(char c);
void sys_nl(void);
void gcursor_move(short x, short y);
void gcursor_left(short n);
void gcursor_right(short n);
void gcursor_up(short n);
void gcursor_down(short n);
void platform_clear_eol(void);
void platform_clear_screen(void);
void platform_home(void);
void platform_standout(int on);
void platform_open(void);
void platform_close(void);
void setraw(void);
void setcook(void);
void setabort(void);
void platform_edit(const char *fname);   /* calls edit_fn if registered */
void flush_output(void);               /* flush buffered output to frontend */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */

