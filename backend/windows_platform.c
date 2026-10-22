// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	windows_platform.c

	Windows 10 / MinGW-w64 replacement for linux_platform.c.

	Provides all platform-specific functions declared in externs.h.
	Structure and section numbering mirrors linux_platform.c exactly
	to make diffs easy to read.

	Key differences from linux_platform.c:
	  - No <unistd.h>, <termios.h>, <sys/wait.h>, <dirent.h>, <fnmatch.h>
	  - Directory iteration via Win32 FindFirstFile / FindNextFile
	  - callsystem() uses CreateProcess()+CREATE_NO_WINDOW (non-blocking,
	    backs quad-EXEC, no console window for console-subsystem targets);
	    waitsystem() uses system() (blocking, backs quad-WEXEC, needed for
	    shell redirection)
	  - time_delay() uses Sleep() instead of usleep()
	  - get_aplhome_dir() uses USERPROFILE fallback, _mkdir() instead of mkdir()
	  - lp_setraw() / lp_setcook() are stubs (Qt owns the terminal)
	  - perform_cd() uses _chdir() / _getcwd()
	  - sys_fdelete() uses _unlink()
*/

#define MSDOS_C 1	/* reuse the guard that suppresses duplicate externs */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

/* Windows-specific headers */
#include <windows.h>
#include <direct.h>		/* _mkdir, _chdir, _getcwd */
#include <io.h>			/* _unlink */
#include <bcrypt.h>		/* BCryptGenRandom - link with -lbcrypt */

#include "parse.h"
#include "mplmem.h"
#include "fkeys.h"
#include "externs.h"
#include "globals.h"

/* Compile-time width check */
typedef char static_assert_int32_is_4_bytes[(sizeof(int32_t) == 4) ? 1 : -1];

/* ============================================================
   Translation layer (orig_trans[]/itrans[]/otrans[], translate_init(),
   untrans_char(), unktrans_char(), ucstrncmp(), alt_input_proc(),
   file_translate()) has moved to platform_translate.c, shared
   byte-for-byte with linux_platform.c. Add platform_translate.c to
   the build; nothing platform-specific was left behind here.
   ============================================================ */

/* ============================================================
   5. Globals
   Windows has no termios; lp_setraw/lp_setcook are stubs since
   the Qt frontend owns all terminal I/O.
   ============================================================ */

static short raw_mode = 0;

char cectable[] = {
    NL_CHAR,0,
    'i',0,
    'h','H',8,0,
    'l','L',12,0,
    'w','W',0,
    'b',0,
    'x','X',0,
    'd',0,
    'D',0,
    '0',0,
    '$',0,
    '.',0,
    '%',0,
    '\033',0,
    'r',0,
    0
};

char ceitable[] = {
    NL_CHAR,0,
    '\033',0,
    8,0,
    127,0,
};

/* ============================================================
   6. Output
   ============================================================ */

void set_width(int n)
{
    WIDTH = n;
}

void dump_output()
{
    fflush(stdout);
}

/* ============================================================
   7. Input
   ============================================================ */

short getach_e()
{
    int c = getchar();
    if (c == EOF)  return 4;
    if (c == 127)  return K_DELETE;
    if (c != 0x1b) return (short)c;

    int c2 = getchar();
    if (c2 != '[') {
        ungetc(c2, stdin);
        return (short)c;
    }
    int c3 = getchar();
    switch (c3) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case '1': getchar(); return K_HOME;
        case '4': getchar(); return K_END;
        case '5': getchar(); return K_PAGEUP;
        case '6': getchar(); return K_PAGEDOWN;
        case '2': getchar(); return K_INSERT;
        case '3': getchar(); return K_DELETE;
        default:  return (short)c;
    }
}

short (*platform_getin_fn)(UCHAR *p) = NULL;

short alt_getin(UCHAR *p)
{
    if (platform_getin_fn)
        return platform_getin_fn(p);
    if (!fgets((char *)p, LINE_SIZE, stdin)) {
        p[0] = '\0';
        ERROR_CODE = SET_ERR(ERR_KILL);
        return 0;
    }
    int n = strlen((char *)p);
    if (n > 0 && p[n-1] == '\n') p[--n] = '\0';
    if (n > 0 && p[n-1] == '\r') p[--n] = '\0';
    return 1;
}

/* ============================================================
   8. System commands

   callsystem() is the non-blocking form (backs quad-EXEC) and
   waitsystem() is the blocking form (backs quad-WEXEC), matching
   linux_platform.c's fork/no-wait vs fork/wait split.

   callsystem() launches via CreateProcess() with CREATE_NO_WINDOW
   and returns as soon as the process is created, without waiting
   for it to finish -- the real Windows equivalent of fork()+exec()
   with no wait(). (An earlier version of this file routed both
   callsystem() and waitsystem() through the blocking system() call,
   which made EXEC block just like WEXEC -- that was a porting
   mistake, not a Windows limitation.)

   CREATE_NO_WINDOW matters for console-subsystem targets (e.g.
   timidity, used to play MIDI files where mplay32.exe once was):
   without it, Windows allocates a brand-new console window for any
   console-subsystem child that doesn't already have one attached --
   independent of ShellExecuteEx vs system(), just how process
   creation works. GUI-subsystem targets like mplay32.exe never hit
   this because they never get a console in the first place.
   CREATE_NO_WINDOW tells Windows not to allocate one at all, so
   there's nothing to show, hide, or flash. (An earlier revision used
   ShellExecuteEx() with SW_HIDE, which can still flash the console
   briefly before hiding it on some systems -- CreateProcess() with
   CREATE_NO_WINDOW avoids that entirely.) Since callsystem() doesn't
   wait on the child, the process/thread handles are closed right
   after a successful launch -- fire-and-forget, no handle leak.

   waitsystem() intentionally still uses system() rather than
   CreateProcess()+WaitForSingleObject(): system() goes through
   cmd.exe, so it understands shell redirection/pipes (">", "|",
   etc.) that CreateProcess() launching the target directly does not,
   and WEXEC callers rely on that. See the parsing logic below for
   how the executable and its argument string are split back into a
   command line for CreateProcess().
   ============================================================ */

short callsystem(char *s)
{
    char *cp;
    int qt;
    char cmdline[2048];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL ok;

    qt = 0;
    if (*s == '\'') {
        s++;
        qt = 1;
    }
    cp = s;
    while (*cp)			/* find divider point to parameters */
    {
        if (*cp == '\'' && qt) break;	/* in a quote, break on next quote */
        if (*cp == ' ' && !qt) break;	/* not in a quote, break on space */
        cp++;
    }
    if (*cp)			/* if we found a break */
    {
        *cp++ = '\0';			/* terminate executable string here */
        while (*cp == ' ') cp++;	/* skip over leading spaces to param string */
    }
    if (!*cp) cp = NULL;	/* no params?  set to NULL */

    /* CreateProcess() wants one command-line string; rebuild it from
       the split exe/params, quoting the exe path in case it contains
       spaces. It also needs a writable buffer, hence the local array
       rather than passing cmdline literally. */
    _snprintf(cmdline, sizeof(cmdline), "\"%s\"%s%s", s, cp ? " " : "", cp ? cp : "");
    cmdline[sizeof(cmdline) - 1] = '\0';

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    ok = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                         CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (ok)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);	/* fire-and-forget: EXEC doesn't wait */
    }
    return (short)ok;
}

int waitsystem(char *s)
{
    return system(s);	/* blocking; also allows shell redirection on the command line */
}

/* do_calldir() (commands.c) now owns everything calldir() used to do
   except the raw enumeration syscall -- see the Session 2 dedup notes
   in PORTING_PLAN.md. This is a thin wrapper so external callers of
   calldir() (commands.c's )LIB/)FNSL handlers) don't need to change.

   TODO: once externs.h is back in front of us, move this prototype
   there next to calldir()'s own declaration instead of redeclaring
   it locally. */
extern void do_calldir(char *s);

void
calldir(char *s)
{
    do_calldir(s);
}

/* ============================================================
   9. Directory iteration
   Uses Win32 FindFirstFile / FindNextFile in place of dirent.
   Returns entries prefixed 'D' (directory) or 'F' (file),
   matching the Linux version's convention.
   ============================================================ */

static HANDLE dir_handle = INVALID_HANDLE_VALUE;
static char   dir_pathbuf[PATHBUF_SIZE];

char *get_first_dir(char *s)
{
    char searchpath[PATHBUF_SIZE + 4];
    WIN32_FIND_DATAA fd;

    if (dir_handle != INVALID_HANDLE_VALUE) {
        FindClose(dir_handle);
        dir_handle = INVALID_HANDLE_VALUE;
    }

    strncpy(dir_pathbuf, (s && s[0]) ? s : ".", sizeof(dir_pathbuf) - 1);
    dir_pathbuf[sizeof(dir_pathbuf) - 1] = '\0';

    snprintf(searchpath, sizeof(searchpath), "%s\\*", dir_pathbuf);
    dir_handle = FindFirstFileA(searchpath, &fd);
    if (dir_handle == INVALID_HANDLE_VALUE) return NULL;

    /* get_next_dir will be called right after; re-open cleanly */
    FindClose(dir_handle);
    dir_handle = FindFirstFileA(searchpath, &fd);
    if (dir_handle == INVALID_HANDLE_VALUE) return NULL;

    /* process the first entry found */
    {
        static char result[PATHBUF_SIZE];
        do {
            if (fd.cFileName[0] == '.') {
                if (!FindNextFileA(dir_handle, &fd)) {
                    FindClose(dir_handle);
                    dir_handle = INVALID_HANDLE_VALUE;
                    return NULL;
                }
                continue;
            }
            snprintf(result, sizeof(result), "%c%s",
                (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'D' : 'F',
                fd.cFileName);
            return result;
        } while (1);
    }
}

char *get_next_dir()
{
    WIN32_FIND_DATAA fd;
    static char result[PATHBUF_SIZE];

    if (dir_handle == INVALID_HANDLE_VALUE) return NULL;

    while (FindNextFileA(dir_handle, &fd)) {
        if (fd.cFileName[0] == '.') continue;
        snprintf(result, sizeof(result), "%c%s",
            (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'D' : 'F',
            fd.cFileName);
        return result;
    }

    FindClose(dir_handle);
    dir_handle = INVALID_HANDLE_VALUE;
    return NULL;
}

void cleanup_dir()
{
    if (dir_handle != INVALID_HANDLE_VALUE) {
        FindClose(dir_handle);
        dir_handle = INVALID_HANDLE_VALUE;
    }
}

/* ============================================================
   10. Filesystem utilities
   ============================================================ */

char *perform_cd(char *s)
{
    static char cwdbuf[PATHBUF_SIZE];
    if (s && *s) _chdir(s);
    _getcwd(cwdbuf, sizeof(cwdbuf));
    return cwdbuf;
}

short sys_fdelete(char *s)
{
    return (short)_unlink(s);
}

short sys_rename(char *s1, char *s2)
{
    return rename(s1, s2) ? -1 : 0;
}

/* ============================================================
   11. Memory utilities
   ============================================================ */

char *sys_malloc(t, l)
short t;
long l;
{
    (void)t;
    return malloc((size_t)l);
}

void sys_mfree(s, l)
char *s;
long l;
{
    (void)l;
    free(s);
}

/* ============================================================
   12. Byte-swap utilities (identical to linux_platform.c)
   ============================================================ */

void long_swap(char *cp)
{
    char c;
    c = cp[0]; cp[0] = cp[3]; cp[3] = c;
    c = cp[1]; cp[1] = cp[2]; cp[2] = c;
}

void short_swap(char *cp)
{
    char c;
    c = cp[0]; cp[0] = cp[1]; cp[1] = c;
}

/* ============================================================
   13. Time
   get_tod() uses standard time() / localtime() — portable.
   time_delay() uses Sleep() (milliseconds) instead of usleep().
   ============================================================ */

int32_t get_tod(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int32_t *lp = (int32_t *)LSCRATCH;

    lp[0] = (int32_t)(t->tm_year + 1900);
    lp[1] = (int32_t)(t->tm_mon  + 1);
    lp[2] = (int32_t)t->tm_mday;
    lp[3] = (int32_t)t->tm_hour;
    lp[4] = (int32_t)t->tm_min;
    lp[5] = (int32_t)t->tm_sec;
    lp[6] = 0;

    return 3600 * lp[3] + 60 * lp[4] + lp[5];
}

void time_delay(long t)
{
    /* Each unit ~93ms: 10 iterations of ~9ms each, matching usleep(9300) on Linux */
    t *= 10;
    while (t-- > 0) {
        Sleep(9);
        if (ABORTSIG) {
            proc_abort(0);
            return;
        }
    }
}

/* ============================================================
   14. Environment
   get_aplhome_dir(): falls back to USERPROFILE (not HOME) on Windows.
   Uses _mkdir() which takes no mode argument.
   ============================================================ */

void
get_mplenv(char *s)
{
    char *cp = getenv(s);
    if (cp) strcpy(s, cp);
    else    s[0] = '\0';
}

void
get_aplhome_dir(char *out, size_t outsz, const char *subdir)
{
    const char *home = getenv("APLHOME");
    if (home && *home) {
        snprintf(out, outsz, "%s/%s/", home, subdir);
    } else {
        /* Windows: prefer USERPROFILE (%USERPROFILE% = C:\Users\name) */
        const char *h = getenv("USERPROFILE");
        if (h && *h)
            snprintf(out, outsz, "%s/apl/%s/", h, subdir);
        else
            snprintf(out, outsz, "./%s/", subdir);
    }
    /* Create directory tree if missing */
    {
        char tmp[LINE_SIZE];
        char *p;
        strncpy(tmp, out, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        int len = strlen(tmp);
        if (len > 1 && (tmp[len-1] == '/' || tmp[len-1] == '\\'))
            tmp[len-1] = '\0';
        for (p = tmp + 1; *p; p++) {
            if (*p == '/' || *p == '\\') {
                *p = '\0';
                _mkdir(tmp);    /* no mode argument on Windows */
                *p = '/';
            }
        }
        _mkdir(tmp);
    }
}

/* ============================================================
   15. Terminal mode
   The Qt frontend owns all I/O so these are stubs on Windows.
   lp_setraw / lp_setcook exist because the backend calls them,
   but they do nothing meaningful in the Qt build.
   ============================================================ */

void lp_setraw()
{
    raw_mode = 1;
}

void lp_setcook()
{
    raw_mode = 0;
}

/* ============================================================
   16. Signal / interrupt
   signal(SIGINT,...) works on Windows with MinGW.
   ============================================================ */

void lp_setabort()
{
    signal(SIGINT, (void (*)(int))proc_abort);
}

int check_for_interrupt(int f)
{
    if (ABORTSIG) {
        proc_abort(f);
        return 1;
    }
    return 0;
}

void handle_line_event(int f)
{
    check_for_interrupt(f);
}

/* ============================================================
   17. Platform lifecycle (identical to linux_platform.c)
   ============================================================ */

void (*platform_edit_fn)(const char *fname) = NULL;

void platform_defaults()
{
    LMARGIN  = 0;
    SCRH     = 32;
    SCRW     = WIDTH = RMARGIN = 82;
    FONTH    = 1;
    FONTW    = 1;
	NEGSIGN  = 0xa2;	/* APL high-minus (overbar) */
}

void platform_edit(const char *fname)
{
    if (platform_edit_fn) platform_edit_fn(fname);
}

/* ============================================================
   18. Cursor stubs
   ============================================================ */

void _clearscreen(int n)
{
    (void)n;
    /* Qt frontend handles display; no-op here */
}

/* ============================================================
   20. Editor
   Default to notepad on Windows if EDITOR is not set.
   In the Qt build, platform_edit_fn is set by the frontend
   and platform_edit() above is what actually gets called.
   ============================================================ */

int calledit(char *s)
{
    char buf[PATHBUF_SIZE];
    const char *ed = getenv("EDITOR");
    if (!ed || !*ed) ed = "notepad";
    snprintf(buf, sizeof(buf), "%s %s", ed, s);
    return system(buf);
}

/* ============================================================
   21. Miscellaneous (identical to linux_platform.c)
   ============================================================ */

void live() { }
void die(short f) { (void)f; }

const char *
get_version(void)
{
    return VERSION;
}

/* ============================================================
   22. Character width functions (identical to linux_platform.c)
   ============================================================ */

short charwidth(char c)
{
    (void)c;
    return FONTW ? FONTW : 1;
}

short stringwidth(char *s)
{
    return (FONTW ? FONTW : 1) * (short)strlen(s);
}

/* ============================================================
   23. Random functions
   ============================================================ */

/* platform_get_seed() -- called by rndsrc.c for quad-RL's unpredictable
   starting state (at first use, and on quad-RL 0). The generator itself
   lives in rndsrc.c, shared by every platform; platform_random()/
   platform_seed()/PLATFORM_RAND_MAX were removed with quad-RL. BCrypt
   is still needed here, so -lbcrypt stays. */
unsigned int platform_get_seed(void)
{
    unsigned int seed;
    if (BCryptGenRandom(NULL, (PUCHAR)&seed, sizeof(seed),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        seed = (unsigned int)get_tod();   /* fallback */
    return seed;
}

