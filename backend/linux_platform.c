// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	linux_platform.c

	Linux/aarch64 replacement for msdos.c.

	Provides all platform-specific functions declared in externs.h that were
	previously implemented against Win32 / Delphi callback infrastructure.

	CONSOLIDATED VERSION:
	- All translation tables from msdos.c and Unit1.pas defined once here
	- kapl_translate.c has been merged in and should be removed from build
	- Original behavior from msdos.c preserved exactly
	- Auto-initializes translation tables on first file_translate() call
*/

#define MSDOS_C 1	/* reuse the guard that suppresses duplicate externs */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#include "parse.h"
#include "mplmem.h"
#include "fkeys.h"
#include "externs.h"
#include "globals.h"

/* Compile-time width check — works in C89/C90 and later */
typedef char static_assert_int32_is_4_bytes[(sizeof(int32_t) == 4) ? 1 : -1];

/* ============================================================
   Translation layer (orig_trans[]/itrans[]/otrans[], translate_init(),
   untrans_char(), unktrans_char(), ucstrncmp(), alt_input_proc(),
   file_translate()) has moved to platform_translate.c, shared
   byte-for-byte with windows_platform.c. Add platform_translate.c to
   the build; nothing platform-specific was left behind here.
   ============================================================ */

/* ============================================================
   5. Globals
   ============================================================ */

static short raw_mode = 0;
static struct termios saved_termios;

/* chedit command key table (same as msdos.c) */
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

/* chedit insert key table */
char ceitable[] = {
    NL_CHAR,0,
    '\033',0,
    8,0,
    127,0,
};

/* ============================================================
   6. Output (terminal.c provides sys_pchar, sys_puts, sys_nl, sys_print)
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

short 
getach_e()
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
    /* fallback for stub/terminal build */
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
   ============================================================ */

short callsystem(char *s)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", s, (char *)NULL);
        _exit(127);
    }
    return 0;
}

int waitsystem(char *s)
{
    return system(s);
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
   ============================================================ */

static DIR *dir_handle = NULL;
static char dir_pathbuf[PATHBUF_SIZE];
static int dir_pathlen = 0;

char *get_first_dir(char *s)
{
    if (dir_handle) { closedir(dir_handle); dir_handle = NULL; }
    const char *path = (s && s[0]) ? s : ".";
    strncpy(dir_pathbuf, path, sizeof(dir_pathbuf) - 2);
    dir_pathlen = strlen(dir_pathbuf);
    dir_handle = opendir(dir_pathbuf);
    if (!dir_handle) return NULL;
    return get_next_dir();
}

char *get_next_dir()
{
    struct dirent *de;
    static char result[PATHBUF_SIZE];

    if (!dir_handle) return NULL;

    while ((de = readdir(dir_handle)) != NULL) {
        const char *nm = de->d_name;
        if (nm[0] == '.') continue;

        char statpath[PATHBUF_SIZE*2];
        snprintf(statpath, sizeof(statpath), "%s/%s", dir_pathbuf, nm);
        struct stat st;
        if (stat(statpath, &st) != 0) continue;

        snprintf(result, sizeof(result), "%c%s",
            S_ISDIR(st.st_mode) ? 'D' : 'F', nm);
        return result;
    }
    return NULL;
}

void cleanup_dir()
{
    if (dir_handle) {
        closedir(dir_handle);
        dir_handle = NULL;
    }
}

/* ============================================================
   10. Filesystem utilities
   ============================================================ */

char *perform_cd(char *s)
{
    static char cwdbuf[PATHBUF_SIZE];

    /* A failed chdir() leaves the directory unchanged; the path returned
       below then shows where we still are, which is the report. If
       getcwd() fails, return an empty string rather than whatever the
       static buffer last held. */
    if (s && *s && chdir(s) != 0) {
        /* nothing more to do -- see above */
    }
    if (!getcwd(cwdbuf, sizeof(cwdbuf))) cwdbuf[0] = '\0';
    return cwdbuf;
}

short sys_fdelete(char *s)
{
    return (short)unlink(s);
}

short sys_rename(char *s1, char *s2)
{
    return rename(s1, s2) ? -1 : 0;
}

/* ============================================================
   11. Memory utilities - match externs.h declarations
   ============================================================ */

/* externs.h: extern char *sys_malloc();  (K&R style - empty params) */
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
   12. Byte-swap utilities
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
    t *= 10;
    while (t-- > 0) {
        usleep(9300);
        if (ABORTSIG) {
            proc_abort(0);
            return;
        }
    }
}

/* ============================================================
   14. Environment
   ============================================================ */

void 
get_mplenv(char *s)
{
    char *cp = getenv(s);
    if (cp) strcpy(s, cp);
    else    s[0] = '\0';
}

/*
 * get_aplhome_dir() - Resolve $APLHOME/<subdir>/ into out[].
 *
 * Priority:
 *   1. $APLHOME/<subdir>/   if $APLHOME is set
 *   2. $HOME/apl/<subdir>/  if $HOME is set
 *   3. ./                   last resort (current directory)
 *
 * Creates the directory if it does not exist.
 * Always returns a path ending in '/'.
 */
void 
get_aplhome_dir(char *out, size_t outsz, const char *subdir)
{
    const char *home = getenv("APLHOME");
    if (home && *home) {
        snprintf(out, outsz, "%s/%s/", home, subdir);
    } else {
        const char *h = getenv("HOME");
        if (h && *h)
            snprintf(out, outsz, "%s/apl/%s/", h, subdir);
        else
            snprintf(out, outsz, "./%s/", subdir);
    }
    /* Create directory (and parents) if missing */
    {
        char tmp[LINE_SIZE];
        char *p;
        strncpy(tmp, out, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        /* strip trailing slash for mkdir loop */
        int len = strlen(tmp);
        if (len > 1 && tmp[len-1] == '/') tmp[len-1] = '\0';
        for (p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }
        mkdir(tmp, 0755);
    }
}

/* ============================================================
   15. Terminal mode
   ============================================================ */

void lp_setraw()
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        saved_termios = t;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN]  = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    raw_mode = 1;
}

void lp_setcook()
{
    if (raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        raw_mode = 0;
    }
}

/* ============================================================
   16. Signal / interrupt
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
   17. Platform lifecycle
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
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
}


/* ============================================================
   20. Editor
   ============================================================ */

int calledit(char *s)
{
    char buf[PATHBUF_SIZE];
    const char *ed = getenv("EDITOR");
    if (!ed || !*ed) ed = "vi";
    snprintf(buf, sizeof(buf), "%s %s", ed, s);
    lp_setcook();
    int r = system(buf);
    if (raw_mode) lp_setraw();
    return r;
}

/* ============================================================
   21. Miscellaneous
   ============================================================ */

void live() { }
void die(short f) { (void)f; }

const char *
get_version(void)
{
    return VERSION;   /* "4.0" from depend.h */
}

/* ============================================================
   22. Character width functions (not provided by terminal.c)
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
   platform_seed()/PLATFORM_RAND_MAX were removed with quad-RL. */
unsigned int platform_get_seed(void)
{
    /* linux version: */
    unsigned int seed;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &seed, sizeof(seed)) != sizeof(seed))
            seed = (unsigned int)get_tod();
        close(fd);
    } else {
        seed = (unsigned int)get_tod();
    }
    return seed;
}

