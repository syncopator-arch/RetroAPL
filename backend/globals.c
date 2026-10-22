// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	globals.c	
	these variables are here if not reentrant, else in wspace.
*/

/*
 * LINE_SIZE, NUM_FKEYS, NUM_FLOATVS, ws_off_t, and all struct forward
 * declarations come from parse.h (single canonical definition).
 * The old local #defines are removed -- parse.h wins.
 */
#include "parse.h"

int term_fd = 0;   /* stdin; legacy terminal fd, unused with platform.h */
short timebase = 6; /* ticks per second */

short g_screenheight;
short g_screenwidth;
short g_fontheight;
short g_fontwidth;
short g_cursorx;
short g_cursory;
short g_syswidth;
short g_digits = 10;
short g_lmargin;
short g_rmargin;
unsigned char *g_commandp;
unsigned char g_linea[LINE_SIZE];
unsigned char g_lineb[LINE_SIZE];
unsigned char g_lined[LINE_SIZE];
short g_debugflag = 0;
short g_error_code;
long g_totscan;
short g_outerflag;
struct ValEnt *g_returnval;
ws_off_t *g_vleft;				/* was struct ValEnt ** -- now points to ws_off_t slot */
ws_off_t *g_vright;
ws_off_t *g_vtemp;
ws_off_t *g_vtmp;
unsigned char *g_ip;
volatile short g_abortsig = 0;
short g_outsync = 0;
short g_ilen;
short g_fnline = 0;
short g_nextline;
short g_func_num = -1;
short g_last_operator = -1;
short g_traceflag = 0;
short g_killme = 0;
short g_guru_abort = 0;
short g_multi = 0;
unsigned short g_seed;
int g_globargc;
char **g_globargv;
struct SymTable *g_symt = 0L;	/* live pointer, recalculated from wspace after realloc */
short g_symtsize = 1024;
short g_symtactual = 0;
short g_num_base = 10;
int32_t *g_mainstack;			/* was long* -- 32-bit stack entries match parse.h masks */
int32_t *g_stackmax;			/* was long* */
short g_srcsize;
short g_indsize;
short g_sortdir = 1;
short g_sortsize;
short g_silent_command;
unsigned char g_vbuf[128];
int32_t g_lscratch[(LINE_SIZE + (int)sizeof(int32_t) - 1) / (int)sizeof(int32_t)];
					/* was long[128] (1024B on 64-bit); converting to int32_t for
					   cross-platform pointer-width portability halved the real
					   byte capacity to 512B as an unintended side effect (element
					   count wasn't revisited). Sized here to LINE_SIZE bytes to
					   restore the original intent -- LSCRATCH is used in several
					   places (sysfuncs.c) as a scratch buffer for one line's worth
					   of character data. */
char g_bscratch[80];
short g_outbase = 10;
short g_aplcase = 1;
char *g_fkeystrings[NUM_FKEYS];
ws_off_t g_floatvs[NUM_FLOATVS];	/* was struct ValEnt* -- GarbCollect adjusts offsets */

unsigned char g_op_stack[60];
short g_lastop;
unsigned char g_linec[LINE_SIZE];
