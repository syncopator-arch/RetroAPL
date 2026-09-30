/*
	crashlog.c

	Fatal-error log: $APLHOME/crash.log, written by fatal_error() (main.c)
	just before the interpreter exits, and read back by the front ends at
	their next startup.

	Why a log instead of a popup: a fatal error means the workspace is
	already known to be damaged, so as little as possible should happen
	afterwards -- no GUI round trips, no dialogs from the engine thread.
	Writing a file is plain C stdio on the engine thread; the next
	startup, with nothing damaged, is where the user gets told.

	What a crash entry holds, in the order it is written (each section is
	flushed before the next starts, so a crash inside a later, riskier
	section still leaves the earlier ones on disk):

	    === APL fatal error <date time> ===     header
	    <the message given to fatal_error()>
	    RetroAPL backend <version>, built <date time>
	    --- workspace ---                       plain header fields
	    --- recent session ---                  the transcript, see below
	    --- SI ---                              si_walk(), capped
	    === end ===

	crash.log is appended to, never rewritten, so it keeps a history.
	When the user acknowledges the notice a front end shows, a line

	    *** acknowledged <date time> ***

	is appended. An entry is "pending" (not yet acknowledged) when no
	acknowledgement follows it; an entry whose writing was cut short (no
	"=== end ===") is still pending, which is the right answer. The file
	is the only record of this state -- nothing is kept in settings.ini.

	Warning entries (V70): an internal inconsistency the interpreter can
	survive -- GarbCollect()'s bad-reference guard -- writes the same
	entry under "=== APL internal warning <date time> ===" through
	crash_log_note(), and the session carries on. Written mid-session, so
	it must leave the interpreter exactly as it found it: the SI walk is
	read-only (si_walk()'s capped form), and CURSX and ABORTSIG, which
	printing and si_walk() touch, are put back. The front ends' notice
	says which kind is pending: a crash, or only warnings.

	Session transcript: an in-memory ring of the last CRASH_RING_SIZE
	bytes of the session -- output as it reaches the front end
	(terminal.c), and each input line as the backend receives it (the
	whole line after Enter, already composed and edited: alt_input_proc()
	in platform_translate.c, and quote-quad input in exec.c's
	proc_quad()). Nothing touches the disk until a crash. Bytes are kept
	as font indices and converted to UTF-8 only when the log is written,
	with kapl_unicode.c (the same table apl_cli and the GUI's clipboard
	use). Output bytes get the same '#' -> quad substitution qt_puts()
	and apl_cli apply; input bytes are stored as typed.

	The prompt the front ends print before an immediate-execution line
	never passes through the backend, so the recorder adds five spaces in
	front of an input line when nothing else is on the current line --
	which is exactly the immediate-execution case (quad input's prompt
	comes from proc_quad() itself and is already recorded; quote-quad
	input continues whatever is on the line).

	The transcript can be switched off (crash_set_transcript(), the GUI's
	Settings dialog). On by default. Only the engine thread touches the
	ring: the setter just sets a flag, and the recorder clears the ring
	itself the first time it sees the flag come back on, so a front end's
	GUI thread never writes into it.
*/

#define CRASHLOG_C 1

#include <time.h>

#include "parse.h"
#include "mplmem.h"
#include "externs.h"
#include "platform.h"
#include "kapl_unicode.h"

#define CRASH_RING_SIZE	32768		/* power of two: index is masked */
#define CRASH_RING_MASK	(CRASH_RING_SIZE - 1)

#define CRASH_SI_NEWEST	20		/* frames shown from the top of the SI */
#define CRASH_SI_OLDEST	5		/* ... and from the bottom */

#define CRASH_HEADER	"=== APL fatal error "
#define CRASH_WARN	"=== APL internal warning "
#define CRASH_ACK	"*** acknowledged "

static unsigned char s_ring[CRASH_RING_SIZE];
static unsigned long s_ring_total = 0;	/* bytes ever recorded; the ring
					   holds the last CRASH_RING_SIZE */
static int s_line_empty = 1;		/* nothing recorded on the current line */
static int s_recording = 1;		/* the recorder's own view of the flag */
static volatile int s_transcript_on = 1;	/* set by crash_set_transcript() */

static int s_dumping = 0;		/* writing the log: record nothing */
static FILE *s_logf = 0;		/* log file, while s_dumping */

/* ------------------------------------------------------------------ */
/*  Recording                                                         */
/* ------------------------------------------------------------------ */

/* Front ends: turn the transcript on or off. Safe from any thread. */
void
crash_set_transcript(int on)
{
	s_transcript_on = on ? 1 : 0;
}

/* Common gate for the recorders: 0 means record nothing. Clears the ring
   when recording is switched back on, so the transcript never spans a
   gap without saying so -- the log reports what it holds. */
static int
record_ok(void)
{
	if (s_dumping) return(0);
	if (!s_transcript_on)
	{
		s_recording = 0;
		return(0);
	}
	if (!s_recording)
	{
		s_ring_total = 0;
		s_line_empty = 1;
		s_recording = 1;
	}
	return(1);
}

static void
ring_put(unsigned char b)
{
	s_ring[s_ring_total & CRASH_RING_MASK] = b;
	s_ring_total++;
}

/* Output on its way to the front end (terminal.c). len bytes of font
   indices, no newlines -- those come through crash_record_nl(). */
void
crash_record_output(const char *s, int len)
{
	unsigned char b;

	if (!s || len <= 0 || !record_ok()) return;
	while (len--)
	{
		b = (unsigned char)*s++;
		if (b == CHAR_QD) b = 0xCC;	/* '#' -> quad glyph, as qt_puts() */
		ring_put(b);
	}
	s_line_empty = 0;
}

void
crash_record_nl(void)
{
	if (!record_ok()) return;
	ring_put('\n');
	s_line_empty = 1;
}

/* One complete input line, font indices, as the backend received it.
   Stops at the first NUL, CR or LF. */
void
crash_record_input(const UCHAR *line)
{
	int i;

	if (!line || !record_ok()) return;
	if (s_line_empty)
		for (i = 0; i < 5; i++) ring_put(' ');	/* the front end's prompt */
	while (*line && *line != 10 && *line != 13)
		ring_put(*line++);
	ring_put('\n');
	s_line_empty = 1;
}

/* ------------------------------------------------------------------ */
/*  Writing                                                           */
/* ------------------------------------------------------------------ */

/* $APLHOME/crash.log, in the same place dbget()'s debug.log goes */
static void
crash_log_path(char *path, size_t pathsz)
{
	size_t n;

	get_aplhome_dir(path, pathsz, "");
	/* an empty subdir gives ".../apl//": drop the doubled separator,
	   since this path is shown to the user */
	n = strlen(path);
	while (n >= 2 && (path[n - 1] == '/' || path[n - 1] == '\\') &&
	       path[n - 2] == path[n - 1])
		path[--n] = '\0';
	if (n + strlen("crash.log") < pathsz)
		strcat(path, "crash.log");
}

static void
timestamp(char *buf, size_t bufsz)
{
	time_t now;
	struct tm *tm;

	now = time(NULL);
	tm = localtime(&now);
	if (!tm || !strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", tm))
		snprintf(buf, bufsz, "(time unknown)");
}

/* One font-index byte to the log as UTF-8. Newline and tab pass through;
   any other control byte is shown as <XX> rather than written raw. */
static void
emit_byte(FILE *f, unsigned char b)
{
	unsigned char one[2];
	char utf8[8];

	if (b == '\n' || b == '\t')
	{
		fputc(b, f);
		return;
	}
	if (b < 0x20 || b == 0x7F)
	{
		fprintf(f, "<%02X>", b);
		return;
	}
	one[0] = b;
	one[1] = '\0';
	kapl_font_to_utf8(one, utf8, sizeof(utf8));
	fputs(utf8, f);
}

/* Output redirected to the log while si_walk() runs: it prints through
   PRINT()/NL() like )SI does, and these receive it. */
static void
log_puts(const char *s)
{
	unsigned char b;

	if (!s_logf || !s) return;
	while ((b = (unsigned char)*s++) != 0)
		emit_byte(s_logf, b == CHAR_QD ? 0xCC : b);
}

static int
log_pchar(char c)
{
	if (s_logf) emit_byte(s_logf, (unsigned char)c);
	return((unsigned char)c);
}

static void
log_nl(void)
{
	if (s_logf) fputc('\n', s_logf);
}

static void
write_transcript(FILE *f)
{
	unsigned long n, start, i;
	unsigned char b;

	if (!s_transcript_on)
	{
		fputs("(session transcript is turned off)\n", f);
		return;
	}
	if (!s_recording || !s_ring_total)
	{
		fputs("(nothing recorded)\n", f);
		return;
	}
	n = s_ring_total;
	start = 0;
	if (n > CRASH_RING_SIZE)
	{
		fprintf(f, "(last %d bytes; earlier session not kept)\n",
			CRASH_RING_SIZE);
		start = n - CRASH_RING_SIZE;
		/* drop the partial line the wrap point cut into */
		for (i = start; i < n; i++)
			if (s_ring[i & CRASH_RING_MASK] == '\n')
			{
				start = i + 1;
				break;
			}
	}
	for (i = start; i < n; i++)
	{
		b = s_ring[i & CRASH_RING_MASK];
		emit_byte(f, b);
	}
	if (s_ring[(n - 1) & CRASH_RING_MASK] != '\n')
		fputc('\n', f);
}

/* Plain reads of the workspace header and the interpreter's position.
   Nothing here follows a pointer into the pool. */
static void
write_workspace(FILE *f)
{
	int i;

	if (!wspace)
	{
		fputs("(no workspace)\n", f);
		return;
	}
	fputs("WSID: ", f);
	for (i = 0; i < WSID_SIZE && wspace->wsid[i]; i++)
		emit_byte(f, (unsigned char)wspace->wsid[i]);
	fputc('\n', f);
	fprintf(f, "wssize=%ld  begin=%08lx  water=%08lx  symtsize=%ld\n",
		(long)wspace->wssize, (unsigned long)(uint32_t)wspace->begin,
		(unsigned long)(uint32_t)wspace->water, (long)wspace->symtsize);
	if (MSTACK && STACKMAX)
		fprintf(f, "MSTACK off=%08lx  STACKMAX off=%08lx  (%ld words in use)\n",
			(unsigned long)((char *)MSTACK - (char *)wspace),
			(unsigned long)((char *)STACKMAX - (char *)wspace),
			(long)(STACKMAX - MSTACK));
	fprintf(f, "FUNC_NUM=%d  FNLINE=%d  ERROR_CODE=%d\n",
		(int)FUNC_NUM, (int)FNLINE, (int)ERROR_CODE);
}

/* The SI, through si_walk() (commands.c), capped at CRASH_SI_NEWEST
   frames from the top and CRASH_SI_OLDEST from the bottom, with a count
   of what was left out. Last, because it is the one section that reads
   through the symbol table into function headers. */
static void
write_si(FILE *f)
{
	void (*save_puts)(const char *);
	int  (*save_pchar)(char);
	void (*save_nl)(void);
	short save_cursx, save_abort;

	if (!wspace || !MSTACK || !STACKMAX || MSTACK > STACKMAX)
	{
		fputs("(no stack)\n", f);
		return;
	}
	flush_output();		/* nothing pending may leak into the log */
	save_puts = platform_puts_fn;
	save_pchar = platform_pchar_fn;
	save_nl = platform_nl_fn;
	save_cursx = CURSX;	/* printing moves it; a warning entry carries on */
	save_abort = ABORTSIG;
	platform_puts_fn = log_puts;
	platform_pchar_fn = log_pchar;
	platform_nl_fn = log_nl;

	ABORTSIG = 0;		/* si_walk() stops on an interrupt */
	if (MSTACK == STACKMAX) fputs("(empty)\n", f);
	else si_walk(1, CRASH_SI_NEWEST, CRASH_SI_OLDEST);
	flush_output();

	platform_puts_fn = save_puts;
	platform_pchar_fn = save_pchar;
	platform_nl_fn = save_nl;
	CURSX = save_cursx;
	ABORTSIG = save_abort;	/* a pending interrupt still gets taken */
}

/* One entry, fatal or warning (header). Returns 1 with the log's path
   in path, or 0 if crash.log couldn't be opened, in which case the entry
   goes to stderr instead (path set to ""). */
static int
write_entry(const char *header, const char *msg, char *path, size_t pathsz)
{
	FILE *f;
	char ts[32];
	int tofile;

	crash_log_path(path, pathsz);
	f = fopen(path, "a");
	tofile = (f != 0);
	if (!f)
	{
		f = stderr;
		path[0] = '\0';
	}
	s_logf = f;
	s_dumping = 1;

	timestamp(ts, sizeof(ts));
	fprintf(f, "%s%s ===\n", header, ts);
	fputs(msg ? msg : "(no message)", f);
	if (!msg || !*msg || msg[strlen(msg) - 1] != '\n') fputc('\n', f);
	fprintf(f, "RetroAPL backend %s, built %s\n", BACKEND_VERSION, BUILD);
	fflush(f);

	fputs("--- workspace ---\n", f);
	write_workspace(f);
	fflush(f);

	fputs("--- recent session ---\n", f);
	write_transcript(f);
	fflush(f);

	fprintf(f, "--- SI (newest first; %d newest and %d oldest frames at most) ---\n",
		CRASH_SI_NEWEST, CRASH_SI_OLDEST);
	fflush(f);
	write_si(f);
	fputs("=== end ===\n", f);
	fflush(f);

	s_dumping = 0;
	s_logf = 0;
	if (tofile) fclose(f);
	return(tofile);
}

/*
	crash_log_write() -- append one crash entry. Called only by
	fatal_error(), which exits right after. See write_entry() for the
	return value.
*/
int
crash_log_write(const char *msg, char *path, size_t pathsz)
{
	return(write_entry(CRASH_HEADER, msg, path, pathsz));
}

/*
	crash_log_note() -- append a warning entry and carry on. For an
	internal inconsistency the interpreter survives, so there is a record
	(and a notice at the next startup) without ending the session. Must
	not be called in the middle of rearranging the workspace: the SI
	section reads function headers through the symbol table. Leaves the
	stack, CURSX and ABORTSIG as they were (see write_si() and
	si_walk()). Returns as write_entry() does.
*/
int
crash_log_note(const char *msg, char *path, size_t pathsz)
{
	return(write_entry(CRASH_WARN, msg, path, pathsz));
}

/* ------------------------------------------------------------------ */
/*  Startup check and acknowledgement (front ends)                    */
/* ------------------------------------------------------------------ */

/*
	crash_log_pending() -- anything nobody has acknowledged? Returns 1 if
	an unacknowledged entry is a crash (fatal error), 2 if the only
	unacknowledged entries are warnings, 0 if nothing is pending (or
	there's no crash.log). path gets the log's path either way. summary,
	if not NULL, gets "<date time>: <first message line>" -- of the most
	recent unacknowledged crash when there is one, otherwise of the most
	recent warning -- for the front end's notice. Only reads the file.
*/
int
crash_log_pending(char *path, size_t pathsz, char *summary, size_t sumsz)
{
	FILE *f;
	char buf[512];
	char when[64];
	char crash_sum[512], warn_sum[512];
	int crash_pending, warn_pending, after_header, is_crash;
	size_t n, hl;

	if (summary && sumsz) summary[0] = '\0';
	crash_log_path(path, pathsz);
	f = fopen(path, "r");
	if (!f) return(0);

	crash_pending = warn_pending = 0;
	after_header = is_crash = 0;
	when[0] = crash_sum[0] = warn_sum[0] = '\0';
	while (fgets(buf, sizeof(buf), f))
	{
		n = strlen(buf);
		while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';

		hl = 0;
		if (!strncmp(buf, CRASH_HEADER, strlen(CRASH_HEADER)))
		{
			hl = strlen(CRASH_HEADER);
			is_crash = 1;
		}
		else if (!strncmp(buf, CRASH_WARN, strlen(CRASH_WARN)))
		{
			hl = strlen(CRASH_WARN);
			is_crash = 0;
		}
		if (hl)
		{
			if (is_crash) crash_pending = 1; else warn_pending = 1;
			after_header = 1;
			/* "YYYY-MM-DD HH:MM:SS ===": a bounded copy, since a
			   hand-edited or damaged line could be longer */
			snprintf(when, sizeof(when), "%.40s", buf + hl);
			n = strlen(when);	/* drop the trailing " ===" */
			if (n >= 4 && !strcmp(when + n - 4, " ===")) when[n - 4] = '\0';
			snprintf(is_crash ? crash_sum : warn_sum, sizeof(crash_sum), "%s", when);
			continue;
		}
		if (!strncmp(buf, CRASH_ACK, strlen(CRASH_ACK)))
		{
			crash_pending = warn_pending = 0;
			crash_sum[0] = warn_sum[0] = '\0';
			after_header = 0;
			continue;
		}
		if (after_header)	/* the line after the header is the message */
		{
			snprintf(is_crash ? crash_sum : warn_sum, sizeof(crash_sum),
				"%.40s: %.400s", when, buf);
			after_header = 0;
		}
	}
	fclose(f);
	if (summary && sumsz)
		snprintf(summary, sumsz, "%s",
			crash_pending ? crash_sum : warn_pending ? warn_sum : "");
	return(crash_pending ? 1 : warn_pending ? 2 : 0);
}

/* Append the acknowledgement line. 0 on success, -1 if crash.log
   couldn't be opened. */
int
crash_log_acknowledge(void)
{
	FILE *f;
	char path[PATHBUF_SIZE];
	char ts[32];

	crash_log_path(path, sizeof(path));
	f = fopen(path, "a");
	if (!f) return(-1);
	timestamp(ts, sizeof(ts));
	fprintf(f, "%s%s ***\n", CRASH_ACK, ts);
	fclose(f);
	return(0);
}
