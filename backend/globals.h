// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	globals.h
*/

/* system variable macros.  Place in wspace-> for reentrancy */

extern int term_fd;

extern short g_screenheight;
extern short g_screenwidth;
extern short g_fontheight;
extern short g_fontwidth;
extern short g_cursorx;
extern short g_cursory;
extern short g_syswidth;
extern short g_digits;
extern short g_lmargin;
extern short g_rmargin;
extern unsigned char *g_commandp;
extern unsigned char g_linea[LINE_SIZE];
extern unsigned char g_lineb[LINE_SIZE];
extern unsigned char g_lined[LINE_SIZE];
extern short g_debugflag;
extern short g_error_code;
extern long g_totscan;
extern short g_outerflag;
extern struct ValEnt *g_returnval;
extern ws_off_t *g_vleft;			/* was struct ValEnt ** -- now points to ws_off_t slot */
extern ws_off_t *g_vright;
extern ws_off_t *g_vtemp;
extern ws_off_t *g_vtmp;
extern unsigned char *g_ip;
extern volatile short g_abortsig;
extern short g_outsync;
extern short g_ilen;
extern short g_fnline;
extern short g_nextline;
extern short g_func_num;
extern short g_last_operator;
extern short g_traceflag;
extern short g_killme;
extern short g_guru_abort;
extern short g_multi;
extern unsigned short g_seed;
extern int g_globargc;
extern char **g_globargv;
extern struct SymTable *g_symt;		/* live pointer, recalculated from wspace after realloc */
extern short g_symtsize;
extern short g_symtactual;
extern short g_num_base;
extern int32_t *g_mainstack;		/* was long* -- 32-bit stack entries match parse.h masks */
extern int32_t *g_stackmax;			/* was long* */
extern short g_srcsize;
extern short g_indsize;
extern short g_sortdir;
extern short g_sortsize;
extern short g_silent_command;
extern unsigned char g_vbuf[128];
extern int32_t g_lscratch[(LINE_SIZE + (int)sizeof(int32_t) - 1) / (int)sizeof(int32_t)];	/* was long[128]; see globals.c for why */
extern char g_bscratch[80];
extern short g_outbase;
extern short g_aplcase;
extern char *g_fkeystrings[NUM_FKEYS];
extern ws_off_t g_floatvs[NUM_FLOATVS];	/* was struct ValEnt* -- GarbCollect adjusts offsets */

extern unsigned char g_op_stack[60];
extern short g_lastop;
extern unsigned char g_linec[LINE_SIZE];

#define SCRH g_screenheight
#define SCRW g_screenwidth
#define FONTH g_fontheight
#define FONTW g_fontwidth
#define CURSX g_cursorx
#define CURSY g_cursory
#define WIDTH g_syswidth
#define DIGITS g_digits
#define LMARGIN g_lmargin
#define RMARGIN g_rmargin
#define COMMANDP g_commandp
#define LINEA g_linea
#define LINEB g_lineb
#define LINED g_lined  /* reserved for debug */
#define DEBUGFLAG g_debugflag
#define ERROR_CODE g_error_code
#define TOTSCAN g_totscan
#define OUTERFLAG g_outerflag
#define CURFUNC ((struct FuncEnt *)WS_PTR(wspace->curfunc))
#define RETURNVAL g_returnval
#define VLEFT g_vleft
#define VRIGHT g_vright
#define VTEMP g_vtemp
#define VTMP g_vtmp
#define IP g_ip
#define ABORTSIG g_abortsig
#define OUTSYNC g_outsync
#define ILEN g_ilen
#define FNLINE g_fnline
#define NEXTLINE g_nextline
#define FUNC_NUM g_func_num
#define LAST_OPERATOR g_last_operator
#define TRACEFLAG g_traceflag
#define KILLME g_killme
#define GURU_ABORT g_guru_abort
#define MULTI g_multi
#define SEED g_seed
#define GLOBARGC g_globargc
#define GLOBARGV g_globargv
#define SYMT g_symt
#define SYMTSIZE g_symtsize
#define SYMTACTUAL g_symtactual
#define NUM_BASE g_num_base
#define MSTACK g_mainstack
#define STACKMAX g_stackmax
#define SRCSIZE g_srcsize
#define INDSIZE g_indsize
#define SORTDIR g_sortdir
#define SORTSIZE g_sortsize
#define SILENT_COMMAND g_silent_command
#define VBUF g_vbuf
#define LSCRATCH g_lscratch
#define BSCRATCH g_bscratch
#define OUT_BASE g_outbase
#define APL_CASE g_aplcase
#define FKEY_STRINGS g_fkeystrings
#define FLOATVS g_floatvs

#define TMP_ME wspace->tmp_me
#define TMP_VE wspace->tmp_ve

#define OP_STACK g_op_stack
#define LASTOP g_lastop
#define LINEC g_linec

