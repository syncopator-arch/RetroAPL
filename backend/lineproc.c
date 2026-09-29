/*
	lineproc.c
*/

#define LINEPROC_C 1

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"
#include "fkeys.h"
#include "externs.h"
#include "platform.h"

#define LINE_KILL 3

extern UCHAR *all_fnames[];

UCHAR var_lo = 'A';
UCHAR var_hi = 'Z';
UCHAR alph_lo = 'a';
UCHAR alph_hi = 'z';

extern UCHAR char_flip();
extern short getach_e();
extern short charwidth();

short
getin(b)
UCHAR *b;
{
#ifdef NO_INPUT_PROC
	return(alt_getin(b));
#else
short c;
short e_li;

	e_li = 0;
	b[0] = 0;

	ledit_startx = CURSX;
	ledit_starty = CURSY;

	for (;;) 
	{
		switch (c = getach_e())
		{
			case LINE_KILL:
				b[0] = 0;
				return(1);
			case 4:			/* eot exit */
				ERROR_CODE = SET_ERR(ERR_KILL);
				b[0] = 0;
				return(0);
			case K_UP:
				break;
			case K_RIGHT:
			case K_HOME:
			case K_END:
			case K_DELETE:
			case K_PAGEUP:
			case K_PAGEDOWN:
				break;
			case K_INSERT:
			case 0x1b:		/* esc - vi editline */

				break;		/* ignore if no line edits */

			case K_LEFT:
			case K_BACKSP:		/* backspace */
				if (e_li) 
				{
					e_li--;
					b[e_li] = 0;	/* always destructive in this mode */
					if (CURSX < charwidth(b[e_li])) {
						CURSX = WIDTH - charwidth(b[e_li]);
						gcursor_up(1);
						gcursor_move(CURSX,0);
					} else gcursor_left(charwidth(b[e_li]));
					PCHAR(' ');
					CURSX += charwidth(' ');
					gcursor_left(charwidth(' '));
				}
				break;
			default:
				if (wspace->caseflip) c = char_flip(c);
				if (c == 9) c = ' ';	/* no tabs */
				if (c == NL_CHAR || c == K_DOWN) {	
					return(1);
				}
				PCHAR((char)c);
				b[e_li++] = (unsigned char)c;
				b[e_li] = 0;
				CURSX += FONTW;
				if (CURSX > WIDTH) NL();
				break;
		}
	}
#endif
}

UCHAR char_xtable[] = {
	'=','=',CHAR_EQ,
	'!','=',CHAR_NE,
/*	'<','<',CHAR_LS,
	'>','>',CHAR_RS, */
	'<','=',CHAR_LE,
	'>','=',CHAR_GE,
	'-','>',CHAR_BRA,
	'o','.',CHAR_OUT,
	'!','<',CHAR_UPG,
	'!','>',CHAR_DNG,
	'\\','o',CHAR_TRA,
	'|','o',CHAR_HROT,
	'-','o',CHAR_VROT,
	'$','=',CHAR_QTAS,
	'#','=',CHAR_QDAS,
	'&','&',CHAR_BAND,
	'|','|',CHAR_BOR,
	'!','~',CHAR_FL,
	'^','^',CHAR_POW,
	'@','{',CHAR_ENC,
	'@','}',CHAR_DEC,
	0,0,0
};

short
input_proc(ins,outs)
UCHAR *ins,*outs;
{
REG UCHAR *cpi,*cpt;
UCHAR *cpo;
UCHAR c,nxtc;
UCHAR *cp;
short i,j,inq,ct,k;

	ct = 0;
	inq = 0;
	cpi = ins;
	cpo = outs;
	while (*cpi == ' ' || *cpi == 9) cpi++;		/* skip space & tab */
	while (*cpi) 
	{
		c = *cpi;
		if (c == CHAR_QT) inq = 1 - inq;
		if (!inq && c == CHAR_COMMENT) break;	/* comment: rest of line ignored, unless quoted */
		if (c == 9) c = ' ';	/* translate tab to space */
		if (c == 10 || c == 13) c = '\0';	/* end of line */
		*cpi++ = c;
		if (c) 
		{
			cp = cpi;
			nxtc = *cp;
			if (!inq) 	/* don't translate while in quotations */
			{
				cpt = char_xtable;
				while (*cpt) 		/* table translate 2 char seq's */
				{
					if (c == *cpt && nxtc == *(cpt + 1)) 
					{
						c = *(cpt + 2);
						cpt += 3;
						cpi++;
						break;
					}
					cpt += 3;
				}
			}
			if (c == CHAR_QD) 		/* quad? check for resvd fn/var */
			{
				short found;
				k = 0;
				found = 0;
				while ((cpt = all_fnames[k]) != NULL) 
				{
					j = 0;
					while (*cpt) 
					{
						i = (short)strlen(cpt);		
						if (!strncmp(cpt,cp,i)) 
						{
							c = cp[i];
							if (!(c >= var_lo && c <= var_hi))
							{
								*cpo++ = (UCHAR)CHAR_SYS;
								ct++;
								c = j + (k << SYSFUNC_SHIFT);
								cpi += i;
								found = 1;
								goto sfound;
							}
						}
						cpt += i + 1;
						j++;
					}
					k++;
				}
sfound:;
				if (!found) c = CHAR_QD;
			}
			*cpo++ = c;
			ct++;
		}
	}
	*cpo = 0;
	return(ct);
}

/*
	Must return result in LINEB as it is assumed by other
	routines.
*/
UCHAR *
input_line()
{
	PRINT("     ");		/* prompt */

	getin(LINEA);

	input_proc(LINEA,LINEB);
	NL();
	return(LINEB);
}

