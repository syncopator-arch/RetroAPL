/*
	main.c
*/

#define MAIN_C 1

#include "depend.h"
#include "parse.h"
#include "mplmem.h"
#include "externs.h"
#include "platform.h"

extern UCHAR *input_line();
extern struct FuncEnt *MakeFNEnt();
extern short mem_init(char *wsname);
extern short do_exec();

short restart_mode = 0;

char *last_error_msg = NULL;
#ifdef AS_DLL
static short xflag;	/* dll_startup()'s; apl_main() has its own local */
#endif

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

#ifdef AS_DLL

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

	ABORTSIG = 0;
	APL_CASE = 1;	/* always default to case translation */
#ifdef NO_INPUT_PROC
	strcpy(LINEB,linebuf);
	line = LINEB;
#else
	line = input_line();
	if (ERROR_CODE == ERR_KILL) return(0);
#endif
	int _rc = exec_nonfn_line(line);
	flush_output();
	return(_rc);
}

void
dll_cleanup()
{
	mem_free();
	die(0);
}

#else	/* not AS_DLL -- standalone interpreter */

void
apl_main(int argc, char* argv[])
{
UCHAR *line;
short s,xflag;
int32_t l;

	GLOBARGC = argc;
	GLOBARGV = argv;
	xflag = s = 1;
	while (s < argc)
	{
		if (argv[s][0] == '-')
		{
			switch (argv[s][1])
			{
				case 'd':
					DEBUGFLAG = 1;
					break;
				case 'x':
					xflag = 0;
					break;
				default:
					fprintf(stderr,"Usage: %s [-d] [-x]\n",argv[0]);
					fprintf(stderr,"  Version %s, Built %s\n",VERSION,BUILD);
					exit(0);
			}
		}
		s++;
	}

	platform_defaults();	/* set SCRH/SCRW/etc before mem_init */
	live();
	prof_clear();

	if (mem_init(NULL) < 1)
	{
		PRINT("Not enough memory");
		NL();
		die(1);
		exit(1);
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
			goto startupfn;
		}
	}

	while (1)
	{
		ABORTSIG = 0;
		APL_CASE = 1;	/* always default to case translation */
		line = input_line();
		if (ERROR_CODE == ERR_KILL) break;
		s = 0;
		while (line[s] == ' ' || line[s] == 9) s++;
		if (!line[0]) continue;
		if (line[s] == ')')
		{
			ERROR_CODE = do_command(line+s);
			if (ERROR_CODE == ERR_EXEC) goto startupfn;
			if (ERROR_CODE == ERR_KILL) break;
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
					/* hold CURFUNC so the entry in the pool won't be
					   garbage collected out */
					CURFUNC->fe_MemEnt.me_reference = (ws_off_t)((char*)&wspace->curfunc - (char*)wspace);
					if (DEBUGFLAG) cdump(CURFUNC->fe_exec);
					if (do_exec(CURFUNC->fe_exec,0)) break;	/* an abort */
					wspace->curfunc = 0;	/* don't need anymore */
				}
			}
		}
	}

	NL();
	mem_free();
	die(0);
}

#endif
