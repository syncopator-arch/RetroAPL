// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	main.c
*/

#define MAIN_C 1

#include "depend.h"
#include "parse.h"
#include "mplmem.h"
#include "externs.h"
#include "platform.h"

extern struct FuncEnt *MakeFNEnt();
extern short mem_init(char *wsname);
extern short do_exec();
extern void seqf_close(void);	/* seqfuncs.c: MIDI sequencer hooks */
extern void seqf_panic(void);

short restart_mode = 0;

char *last_error_msg = NULL;
static short xflag;	/* dll_startup()'s */

int startup_fn = 0;

void
cdump(s)
UCHAR *s;
{
short l;
char *cp;

	cp = (char *)LSCRATCH;
	NL();
	l = (*s << 8) + *(s + 1);
	l -= 2;	/* length includes count */
	sprintf(cp,"[%d]  ",l);
	PRINT(cp);
	s += 2;
	while (l)
	{
		sprintf(cp,"%02x ",*s++);
		PRINT(cp);
		l--;
	}
	NL();
}

/*
	fatal_error() -- an internal inconsistency the interpreter can't
	continue from (a damaged workspace). Writes a crash entry to
	$APLHOME/crash.log (crashlog.c), shows the message in the session
	window / on apl_cli's stdout, and ends the process with
	FATAL_EXIT_CODE. Never returns.

	msg may run to several lines ('\n'-separated); callers include the
	specifics (offsets, sizes) that would help diagnose it.

	Order: the log first, since the file outlives the process and the
	window doesn't; then the terminal, which is what apl_cli and test
	transcripts see. In the GUI the window closes right after, and the
	next startup reports the crash (main.cpp).

	Exits with _Exit() after fflush(NULL): no atexit handlers or static
	destructors, which in the Qt build would run on this (engine) thread
	while the GUI thread is still live -- tested V70: exit() from the
	engine thread tears Qt objects down from the wrong thread. fflush(NULL)
	keeps what a normal exit would: open files' buffered data and stdout.
	A second fatal error while this one is being handled (the SI walk
	tripping over the damage, say) exits at once.

	Both exits call seqf_panic() (seqfuncs.c) just before _Exit(),
	after the log and the message, so notes the MIDI sequencer has
	sounding don't hang on the synth: note-offs and All Notes Off,
	without waiting long for the sequencer's lock. It touches nothing
	in the workspace, and does nothing unless a port is open. The
	kernel closes the port itself after _Exit().

	Not die(): die(short) stays the normal-shutdown call, and has no
	message to give.
*/
#define FATAL_EXIT_CODE 70	/* EX_SOFTWARE (sysexits.h): internal software error */

void
fatal_error(const char *msg)
{
static int in_fatal = 0;
char path[PATHBUF_SIZE];
char line[LINE_SIZE];
const char *p, *e;
size_t n;
int logged;

	if (in_fatal)
	{
		seqf_panic();
		fflush(NULL);
		_Exit(FATAL_EXIT_CODE);
	}
	in_fatal = 1;

	flush_output();		/* a partial line goes into the transcript too */
	logged = crash_log_write(msg, path, sizeof(path));

	NL();
	PRINT("FATAL INTERNAL ERROR -- APL must exit");
	NL();
	for (p = msg ? msg : ""; *p; p = *e ? e + 1 : e)
	{
		e = strchr(p, '\n');
		if (!e) e = p + strlen(p);
		n = (size_t)(e - p);
		if (n >= sizeof(line)) n = sizeof(line) - 1;
		memcpy(line, p, n);
		line[n] = '\0';
		PRINT(line);
		NL();
	}
	if (logged)
	{
		PRINT("Details written to ");
		PRINT(path);
		NL();
	}
	flush_output();
	seqf_panic();
	fflush(NULL);
	_Exit(FATAL_EXIT_CODE);
}

int
dll_startup(int debug, int xf, char *wsname, int argc, char **argv)
{
short s;
int32_t l;

	GLOBARGC = argc;
	GLOBARGV = argv;

	DEBUGFLAG = debug;
	xflag = xf;

	live();
	prof_clear();

	if (mem_init(wsname) < 1)
	{
		last_error_msg = "Not enough memory";
		die(1);
		return(-1);
	}

	SEED = (unsigned short)get_tod();
	if (!SEED) SEED = 1010;

	FNLINE = 0;

	s = wspace->bootfn;
	if (s != -1 && xflag)
	{
		LINEA[0] = 0;
		LINEA[1] = 4;
		LINEA[2] = NFUN_CODE | s >> 8;
		LINEA[3] = s & 0xff;
		l = (int32_t)sizeof(struct FuncEnt) - (int32_t)sizeof(struct MemEnt);
		wspace->curfunc = WS_OFF(MakeEnt(l,ME_FUNCN,(ws_off_t *)0));
		if (wspace->curfunc)
		{
			memcpy((char *)CURFUNC->fe_exec,(char *)LINEA,4);
			CURFUNC->fe_NextLine = 0;
			startup_fn = 1;
			return(1);	/* indicate a startup function pending */
		}
	}
	ABORTSIG = 0;
	return(0);
}

int
exec_nonfn_line(UCHAR *line)
{
short s;

	if (!line) goto startupfn;	/* executing boot fn */

	/* skip past any leading whitespace so we can determine if it's a command */
	s = 0;
	while (line[s] == ' ' || line[s] == 9) s++;
	if (!line[0]) return(0);	/* nuttin' to do */

	if (line != LINEB)	/* insure we're using LINEB so MakeFNEnt works */
	{
		strcpy(LINEB,line);
		line = LINEB;
	}

	if (line[s] == ')')
	{
		ERROR_CODE = do_command(line+s);
		if (ERROR_CODE == ERR_EXEC) goto startupfn;
		if (ERROR_CODE == ERR_KILL) return(-9);
		if (ERROR_CODE)
		{
			message(ERROR_CODE);
			NL();
		}
	} else {
		{ struct FuncEnt *_cf = MakeFNEnt(); wspace->curfunc = _cf ? WS_OFF(_cf) : 0; }	/* comps LINEB into wspace ent */
		if (!ABORTSIG)
		{
			if (!wspace->curfunc)
			{
				if (ERROR_CODE) message(ERROR_CODE);
			} else {
startupfn:;
				startup_fn = 0;	/* clear flag if set */

				/* hold CURFUNC so the entry in the pool won't be
				   garbage collected out */
				CURFUNC->fe_MemEnt.me_reference = (ws_off_t)((char*)&wspace->curfunc - (char*)wspace);
				if (DEBUGFLAG) cdump(CURFUNC->fe_exec);
				s = do_exec(CURFUNC->fe_exec,0);
				if (s) return(s);	/* an abort */
				wspace->curfunc = 0;	/* don't need anymore */
			}
		} else {
			if (DEBUGFLAG)
			{
				NL();
				PRINT("ABORTSIG set.");
			}
		}
	}
	return(0);
}

int
exec_apl_line(UCHAR *linebuf)
{
UCHAR *line;

	if (startup_fn) return(exec_nonfn_line((UCHAR *)0)); /* startup fn pending */

	/* A typed line: the person pressed Enter, so the front end is at
	   the start of a new line, but its prompt, echo and newline don't
	   go through NL(). Without this, CURSX kept whatever the last
	   output left -- after ⍞←'NAME? ' the next line's output wrapped
	   6 columns early -- and message()'s CURSX test would put a
	   blank line before the caret. Same reason as proc_quad()'s
	   reset after quad input (exec.c). */
	CURSX = 0;
	ABORTSIG = 0;
	APL_CASE = 1;	/* always default to case translation */
	strcpy(LINEB,linebuf);
	line = LINEB;
	int _rc = exec_nonfn_line(line);
	flush_output();
	return(_rc);
}

void
dll_cleanup()
{
	seqf_close();	/* MIDI sequencer: stop, note-offs, close the port */
	mem_free();
	die(0);
}
