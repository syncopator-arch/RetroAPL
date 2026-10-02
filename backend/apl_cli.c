/*
	apl_cli.c

	Command-line front end for the APL backend, with UTF-8 input and
	output. Built mainly for regression testing: it drives the backend
	through the same AS_DLL entry points, in the same order, as the Qt
	front end's EngineThread::run() in main.cpp, so tests exercise the
	same code the GUI does:

	    UTF-8 line -> font-index bytes (kapl_unicode.c, as the GUI's paste)
	               -> alt_input_proc()   (font index -> internal code)
	               -> exec_apl_line()    (-9 means )OFF)

	Output arrives as font-index bytes through the puts callback and is
	converted to UTF-8 (as the GUI's copy), after the same CHAR_QD ('#')
	-> quad substitution qt_puts() applies.

	Usage:
	    apl_cli [-t] [-w wsname] [-q both|warn|new] [args...]

	    -t         transcript mode: echo each input line after the
	               five-space prompt, so stdout reads like a GUI session.
	               This is what the regression tests use.
	    -w wsname  workspace name passed to dll_startup(); default "",
	               the same as the GUI with no workspace argument.
	    -q mode    quad-function syntax (quad_syntax, parse.c), same as
	               the GUI's -q: both (default) accepts ⎕IO 0 and
	               ⎕IO←0; warn also has )ENTER note each old-form use;
	               new accepts only ⎕IO←0.
	    args       passed through to the backend for quad-ARGS.

	Without -t, prompts are shown only when stdin is a terminal, so
	`apl_cli < script.apl` prints just the results.

	Graphics are not supported yet: grfuncs.o is not linked, and the
	placeholders at the end of this file (copied from the platform
	files' stub_test block) stand in for it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "parse.h"
#include "platform.h"
#include "kapl_unicode.h"

/* Backend entry points (main.c, AS_DLL build) -- declared locally, as
   main.cpp does, since no backend header declares them. */
extern int  dll_startup(int debug, int xflag, char *wsname, int argc, char **argv);
extern int  exec_apl_line(UCHAR *linebuf);
extern void dll_cleanup(void);
extern int  crash_log_pending(char *path, size_t pathsz, char *summary, size_t sumsz);	/* crashlog.c */
/* quad_syntax, QSYN_*: declared in parse.h, defined in parse.c */

#define PROMPT		"     "		/* matches the GUI's INDENT */
#define RAW_SIZE	(LINE_SIZE * 4)	/* UTF-8 is up to 4 bytes per character */

static int transcript = 0;	/* -t */
static int show_prompt = 0;	/* -t, or stdin is a terminal */
static int input_eof = 0;
static long line_no = 0;	/* input line number, for messages */

/* ------------------------------------------------------------------ */
/*  Messages go to stderr; flush stdout first so that when the test   */
/*  runner merges the two streams, messages land in the right place.  */
/* ------------------------------------------------------------------ */

static void
cli_warn(const char *msg)
{
	fflush(stdout);
	fprintf(stderr, "apl_cli: line %ld: %s\n", line_no, msg);
}

/* ------------------------------------------------------------------ */
/*  Input                                                              */
/* ------------------------------------------------------------------ */

/*
 * Read one line of UTF-8 from stdin into raw, without its line ending.
 * An over-long line is truncated and the rest of it discarded. Returns
 * 0 at end of input.
 */
static int
read_line(char *raw, int size)
{
	int n, c;

	fflush(stdout);
	if (!fgets(raw, size, stdin)) {
		raw[0] = '\0';
		return 0;
	}
	line_no++;
	n = (int)strlen(raw);
	if (n > 0 && raw[n - 1] == '\n') {
		raw[--n] = '\0';
	} else if (!feof(stdin)) {
		while ((c = getchar()) != EOF && c != '\n')
			;
		cli_warn("line too long, truncated");
	}
	if (n > 0 && raw[n - 1] == '\r') raw[--n] = '\0';

	/* skip a UTF-8 byte-order mark at the start of the input */
	if (line_no == 1 && (unsigned char)raw[0] == 0xEF &&
	    (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
		memmove(raw, raw + 3, strlen(raw + 3) + 1);
	return 1;
}

/* UTF-8 -> font-index bytes, into a buffer of LINE_SIZE */
static void
convert_line(const char *raw, UCHAR *font)
{
	int status;

	kapl_utf8_to_font(raw, font, LINE_SIZE, &status);
	if (status & KAPL_TRUNCATED)
		cli_warn("line too long, truncated");
	if (status & KAPL_BADCHARS)
		cli_warn("unrecognized characters replaced with '?'");
}

/*
 * Transcript echo of an input line, shown as the backend received it
 * (converted back from font-index bytes), so aliases and '?'
 * substitutions are visible -- as a paste into the GUI would show.
 */
static void
echo_line(const UCHAR *font)
{
	char utf8[RAW_SIZE];

	if (!transcript) return;
	kapl_font_to_utf8(font, utf8, sizeof utf8);
	fputs(utf8, stdout);
	putchar('\n');
}

/*
 * Quad input (getin_fn). Returns font-index bytes, the same as the
 * GUI's qt_getin(); the backend runs alt_input_proc() on them itself.
 * At end of input, returns 0 as qt_getin() does on shutdown, and also
 * sets ABORTSIG so a function waiting on quad input is interrupted
 * rather than left waiting for input that will never come.
 */
static short
cli_getin(UCHAR *p)
{
	char raw[RAW_SIZE];

	flush_output();
	if (!read_line(raw, sizeof raw)) {
		p[0] = '\0';
		input_eof = 1;
		ABORTSIG = 1;
		return 0;
	}
	convert_line(raw, p);
	echo_line(p);
	return 1;
}

/* ------------------------------------------------------------------ */
/*  Output                                                             */
/* ------------------------------------------------------------------ */

static void
put_font_byte(unsigned char b)
{
	unsigned char one[2];
	char utf8[8];

	one[0] = (b == 0x23) ? 0xCC : b;	/* CHAR_QD -> quad glyph, as qt_puts() */
	one[1] = '\0';
	kapl_font_to_utf8(one, utf8, sizeof utf8);
	fputs(utf8, stdout);
}

static void
cli_puts(const char *s)
{
	const unsigned char *p;

	for (p = (const unsigned char *)s; *p; p++)
		put_font_byte(*p);
}

static int
cli_pchar(char c)
{
	put_font_byte((unsigned char)c);
	return (unsigned char)c;
}

static void
cli_nl(void)
{
	putchar('\n');
}

/* Cursor and screen control: no-ops for line-oriented output */
static void cli_cursor_move(short x, short y) { (void)x; (void)y; }
static void cli_cursor_left(short n)  { (void)n; }
static void cli_cursor_right(short n) { (void)n; }
static void cli_cursor_up(short n)    { (void)n; }
static void cli_cursor_down(short n)  { (void)n; }
static void cli_clear_eol(void)       { }
static void cli_clear_screen(void)    { }
static void cli_home(void)            { }
static void cli_standout(int on)      { (void)on; }

/* Lifecycle and terminal mode: no-ops, as in the Qt front end */
static void cli_open(void)     { }
static void cli_close(void)    { }
static void cli_setraw(void)   { }
static void cli_setcook(void)  { }
static void cli_setabort(void) { }

/* Ctrl-C sets ABORTSIG directly, as the Qt front end does */
static void
cli_sigint(int sig)
{
	(void)sig;
	ABORTSIG = 1;
}

static const PlatformFuncs cli_funcs = {
	.puts_fn         = cli_puts,
	.pchar_fn        = cli_pchar,
	.nl_fn           = cli_nl,
	.cursor_move_fn  = cli_cursor_move,
	.cursor_left_fn  = cli_cursor_left,
	.cursor_right_fn = cli_cursor_right,
	.cursor_up_fn    = cli_cursor_up,
	.cursor_down_fn  = cli_cursor_down,
	.clear_eol_fn    = cli_clear_eol,
	.clear_screen_fn = cli_clear_screen,
	.home_fn         = cli_home,
	.standout_fn     = cli_standout,
	.open_fn         = cli_open,
	.close_fn        = cli_close,
	.setraw_fn       = cli_setraw,
	.setcook_fn      = cli_setcook,
	.setabort_fn     = cli_setabort,
	.edit_fn         = NULL,	/* )EDIT not supported */
	.getin_fn        = cli_getin,
};

/* ------------------------------------------------------------------ */
/*  main -- mirrors EngineThread::run() in main.cpp                   */
/* ------------------------------------------------------------------ */

static void
usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-t] [-w wsname] [-q both|warn|new] [args...]\n", prog);
	exit(2);
}

/*
	An earlier session's fatal error (or, failing that, internal warning)
	that nobody has acknowledged (see crashlog.c): one line on stderr,
	informational only. Acknowledging
	is left to the GUI's startup notice, so this repeats until then.
	The session transcript is always on here (crashlog.c's default;
	apl_cli has no setting for it).
*/
static void
report_pending_crash(void)
{
	char path[PATHBUF_SIZE];
	char summary[256];

	int kind = crash_log_pending(path, sizeof path, summary, sizeof summary);

	if (kind) {
		fflush(stdout);
		fprintf(stderr, "apl_cli: an earlier session %s (%s) -- see %s\n",
			kind == 1 ? "ended with a fatal error" : "reported an internal warning",
			summary, path);
	}
}

int
main(int argc, char **argv)
{
	char *wsname = "";
	char raw[RAW_SIZE];
	UCHAR font[LINE_SIZE];
	UCHAR exec_line[LINE_SIZE];
	UCHAR empty[2] = { 0, 0 };
	int i, rc;

	/* options; everything after them is passed through for quad-ARGS */
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-t") == 0) {
			transcript = 1;
		} else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
			wsname = argv[++i];
		} else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
			i++;
			if (strcmp(argv[i], "both") == 0) quad_syntax = QSYN_BOTH;
			else if (strcmp(argv[i], "warn") == 0) quad_syntax = QSYN_WARN;
			else if (strcmp(argv[i], "new") == 0) quad_syntax = QSYN_NEW;
			else usage(argv[0]);
		} else if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		} else {
			usage(argv[0]);
		}
	}
	/* keep argv[0] as the program name, followed by the pass-through args */
	argv[i - 1] = argv[0];
	argc -= i - 1;
	argv += i - 1;

	show_prompt = transcript || isatty(STDIN_FILENO);
	signal(SIGINT, cli_sigint);

	platform_register(&cli_funcs);
	platform_defaults();
	report_pending_crash();

	rc = dll_startup(0, 1 /* run boot fn if present */, wsname, argc, argv);
	if (rc < 0) {
		fflush(stdout);
		fprintf(stderr, "apl_cli: workspace initialisation failed\n");
		return 1;
	}
	if (rc == 1)
		exec_apl_line(empty);	/* pending startup function */

	while (!input_eof) {
		if (show_prompt) fputs(PROMPT, stdout);
		if (!read_line(raw, sizeof raw)) {
			if (show_prompt) putchar('\n');
			break;
		}
		convert_line(raw, font);
		echo_line(font);
		alt_input_proc(font, exec_line);
		if (exec_apl_line(exec_line) == -9) break;	/* )OFF */
	}

	dll_cleanup();
	fflush(stdout);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Graphics placeholders -- copied from the #ifndef AS_DLL block in  */
/*  linux_platform.c / windows_platform.c, which provides them for    */
/*  stub_test. The backend objects linked here are the AS_DLL ones,   */
/*  where that block is compiled out, and grfuncs.o is left out (its  */
/*  gw_*() calls resolve only against the Qt front end), so this file */
/*  supplies the same set. Graphics calls do nothing; replace these   */
/*  when graphics testing is taken up.                                */
/* ------------------------------------------------------------------ */

int flush_graphics()                    { return 0; }
void sys_ctrl(long mode)                { (void)mode; }
long sys_font_families(char *buf, long bufsize) { (void)buf; (void)bufsize; return -1; }
long graph_ctrl(long a,long b,long c,long d) { return 0; }
void sys_title(unsigned char *cp)        { }
long open_window(char *cp,long x,long y,long w,long h) { return 0; }
void close_window(long w)               { }
int set_gpen(long a,long b)            { return 0; }
int clear_window(long w,long c)        { return 0; }
int g_move(long w,long x,long y)       { return 0; }
int g_draw(long w,long x,long y)       { return 0; }
int g_sendpoints(long csz,long len,char *datap) { return 0; }
void g_windowsize(long *lp)             { lp[0]=lp[1]=lp[2]=0; }
short w_getnums()                       { return 0; }
void set_window_title(long w,char *cp)  { }
UCHAR graph_fnames[] = { 0 };
UCHAR graph_ftypes[] = { 0 };
short graph_funcs() { return(0); }
