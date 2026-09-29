/*
    vprint.c
*/

#define VPRINT_C 1

#include "parse.h"
#include "mplmem.h"
#include "platform.h"
#include "externs.h"

char *graphic_on = 0;
char *graphic_off = 0;
short graph_flag = 0;
short att_space = 0;

unsigned char NEGSIGN = '-';

/* default char based graphic symbols */
char graph_chars[]  = "-|+++++++++";

/*
	print routine that handles special symbols (graphics chars),
	special functions (cursor movements) 
	and line wrapping?

	s - print string
	f - flag to enable word wrap
	ln - length -- it appears string is not necessarily null terminated
*/
void
vout(UCHAR *s, short f, int ln)
{
unsigned char b,*cp;
short i;
char multbuf[8];

	while (ln--) 
	{
		if (MULTI) 	/* a multi char seq */
		{
			multbuf[MULTI-1] = *s;
			if (MULTI > 1) 
			{
				CURSX = (short)multbuf[0];
				gcursor_move(CURSX,(short)multbuf[1]);
				CURSX--;
				MULTI = 0;
			} else MULTI++;
		} else {
			switch (*s) 
			{
				case 1:     /* clear */
					flush_output();
					platform_clear_screen();
					gcursor_move(0,(short)(FONTH-1));
					flush_output();
					break;
				case 2:     /* home */
					flush_output();
					platform_home();
					gcursor_move(0,(short)(FONTH-1));
					break;
				case 3:		/* cursor up */
					gcursor_up(1);
					break;
				case 4:		/* cursor down */
					gcursor_down(1);
					break;
				case 5:		/* cursor left */
					if (CURSX) gcursor_left(1);
					break;
				case 9:		/* tab */
				case 6:		/* cursor right */
					if (CURSX < WIDTH) gcursor_right(1);
					break;
				case 14:	/* position cursor */
					MULTI = 1;
					break;
				case 15:	/* clear to eol */
					platform_clear_eol();
					break;
				case 16:	/* horiz line */
				case 17:	/* vert line */
				case 18:	/* cross */
				case 19:	/* bottom tee */
				case 20:	/* top tee */
				case 21:	/* lower left crnr */
				case 22:	/* upper left crnr */
				case 23:	/* upper right crnr */
				case 24:	/* lower right crnr */
				case 25:	/* left tee */
				case 26:	/* right tee */
					if (graphic_on && !graph_flag) 
					{
						platform_standout(1);
						graph_flag = 1;
					}
					sys_pchar((unsigned char)graph_chars[*s - 16]);
					break;
				case 28:	/* normal attrib */
					platform_standout(0);
					graph_flag = 0;	/* assumes exit of graphics */
					/* always takes up a space */
					if (!att_space) PRINT((unsigned char *)" ");
					break;
				case 29:	/* standout attrib */
					if (graph_flag) platform_standout(0);
					graph_flag = 0;
					/* always takes up a space */
					if (!att_space) PRINT((unsigned char *)" ");
					platform_standout(1);
					break;
				case NL_CHAR:
					NL();
					break;
				case OTHER_CHAR:	/* ignore other eol */
					break;
				default:
					cp = (unsigned char *)s;
					i = 0;
					while (*cp++ > 31 && i<=ln) i++;
					if (i >= WIDTH) 
					{
						i = WIDTH - CURSX;
						b = s[i];
						s[i] = '\0';
						PRINT(s);
						s[i] = b;
						NL();
					} else {
						if (f && (CURSX + i) >= WIDTH) NL();
						if (CURSX >= WIDTH) NL();
						i = MIN(i,WIDTH - CURSX);
						if (i) 
						{
							b = s[i];
							s[i] = '\0';
							PRINT(s);
							s[i] = b;
						} else i = 1;
					}
					s += i - 1;
					ln -= i - 1;
					break;
			}
		}
		s++;
	}
}

short
floatlen(double f, short w, int *dpp, int *efp)
{
short i,j;
double t;
short digits;
short fprec;
short trim;

/* determine if should use E notation */
	if (f >= 0.0) t = f;
	else t = 0.0 - f;

	if (t != 0.0 && (t > 1000000.0 || t < 0.0000001)) *efp = 1;	/* use E notation */
	else *efp = 0;									/* don't use E */

	/* DIGITS ()DIGITS setting) is APL2 ⎕PP-style Printing Precision: the
	   number of digits printed to the right of the decimal point (e.g.
	   ⎕PP←2 for dollars and cents). Its sign is the trim mode switch:
	   DIGITS>0 means *up to* that many decimals, trailing zeros trimmed
	   (natural-arithmetic style, and the default sense since the
	   default setting is +10); DIGITS<0 means exactly that many
	   decimals, trailing zeros kept (fixed-precision/currency style,
	   e.g. )DIGITS ¯2 for dollars and cents). 0 is a valid setting
	   either way (integer display, no decimal point); clamp
	   defensively in case DIGITS was ever set to something invalid
	   otherwise. */
	digits = DIGITS;
	trim = 1;
	if (digits < 0) { trim = 0; digits = 0 - digits; }
	if (digits > 17) digits = 17;

	if (*efp)
	{
		/* E notation: mantissa is always d.ddd..., one digit before the
		   point -- DIGITS digits after it, same meaning as F notation
		   below. */
		fprec = digits;
		sprintf((char *)LINEA,"%20.*e",fprec,f);
	} else {
		/* F notation: DIGITS digits after the decimal point, full stop --
		   not derived from the integer part's width. */
		fprec = digits;
		sprintf((char *)LINEA,"%20.*f",fprec,f);
	}

	/* if no length specified, remove leading spaces */
	if (w == -1) 
	{
		w = 0;
		while (LINEA[w] == ' ') w++;
		if (w) memmove(LINEA,&LINEA[w],30);	/* overlapping: memmove */
		w = 0;
	}

	i = strlen((char *)LINEA) - 1;
	if (*efp) 
	{
		/* find where the E is in the result */
		while (i && LINEA[i] != 'e') i--;	
		j = i;
		i--;
	}

	/* By default (positive DIGITS) trailing zeros are trimmed down to
	   whatever precision each value actually needs -- e.g. so a
	   matrix's shared column width can still drop zeros that are
	   trailing across every element in that column. Only keep them
	   when the trim mode switch (negative DIGITS) asked for a fixed
	   decimal-place count instead -- ⎕PP←¯2 must always show two
	   decimals (e.g. "3.00"), not collapse to "3". */
	if (trim) while (LINEA[i] == '0') i--;
	if (LINEA[i] != '.') i++;

	if (*efp) 		/* edit e notation */
	{
		/* reconstruct the E notation after pruning the decimal */
		LINEA[i++] = 'E';
		if (LINEA[++j] == '-') LINEA[i++] = '-';
		if (LINEA[++j] != '0') LINEA[i++] = LINEA[j];

		do {
			LINEA[i++] = LINEA[++j];
		} while (LINEA[j]);
	} else LINEA[i] = '\0';

	if (w) 
	{
		/* if width specified, set it */
		w = i - w;
		if (w) memmove(LINEA,&LINEA[w],30);	/* overlapping: memmove */
		i = strlen((char *)LINEA);
	}

	j = 0;

	while (LINEA[j] && (LINEA[j] != '.')) j++;

	*dpp = j + 1;

	return(i+1);
}


/* convert lowercase E to uppercase, & remove leading zeros in E value */
void
convert_numstring(unsigned char *pp, short w)
{
unsigned char *cp,*sp;

	cp = pp;	/* output pointer */
	sp = cp;	/* input pointer */
	while (*cp)
	{
		if (*sp == '-') *sp = NEGSIGN;	/* convert neg sign to proper char */
		if (*sp == 'e') *sp = 'E';	
		if (*sp == 'E') 
		{
			/* backup to chop out any trailing zeroes */
			cp--;
			while (cp > pp && *cp == '0') cp--;
			if (*cp == '.') cp += 2;
			else cp++;
			*cp++ = 'E';			/* insert the E */
			sp++;

			if (*sp == NEGSIGN || *sp == '-')
			{
				sp++;
				*cp++ = NEGSIGN;	/* a neg exponent, pass it thru */
			}
			if (*sp == '+') sp++;			/* a pos exponent, skip over */

			while (*sp == '0') sp++;	/* skip over exponent leading zeros */
			if (!*sp || *sp == ' ') *cp++ = '0';	/* gotta have at least one exponent digit */

			while (*sp && *sp != ' ') *cp++ = *sp++;	/* copy in remaining digits */
			while ((cp - pp) < w) *cp++ = ' ';	/* padding at end */

			*cp++ = *sp++;					/* terminate */
		} else {
			*cp++ = *sp++;
		}
	}
}

/*
	"variable" print - prints out an M entry, w/formatting
*/
void
vprint(struct MemEnt *me)
{
unsigned char *cp;
struct ValEnt *ve;
long l,t,rc;
int32_t m;
short i,j,rank,lc,w = 0,bm;	/* w: set and used only when rank > 1 */
union MultiP dp;
int icheck;
double dm = 0.0;	/* set and used only when bm == FLOATSIZE */
int dpd,wmax,dpmax,ep,use_e;
char ffmt[30];
	

	icheck = 0;

	use_e = dpmax = wmax = 0;

	ve = (struct ValEnt *)me;

	if ((ve->ve_vtype | VARIABLE_BIT) == STRUCT_CODE) 
	{
		l = ve->ve_length;
		dp.cp = DATA_AD(ve);
		while (l--) 
		{
			if (*dp.lp) 
			{
				vprint(WS_PTR(*dp.lp));
				if (l) NL();
			}
			dp.lp++;
		}
		return;
	}

	rank = ve->ve_rank;
	bm = ve->ve_vtype & BYTES_MASK;

	l = 1;
	for (i=0;i<rank;i++) l *= ve->ve_rho[i];
	dp.cp = DATA_AD(ve);

if (rank > 1) 
{
	/* find largest width by passing all through floatlen() */
	w = 2;
	for (t=0L;t<l;t++) 
	{
		switch (bm) 
		{
			case 1:
				m = (long)(signed char)*dp.cp++;
				break;
			case 2:
				m = (long)*dp.sp++;
				break;
			case 4:
				m = *dp.lp++;
				break;
			case FLOATSIZE:
				dm = *dp.dp++;
				break;
		}
		if (bm == FLOATSIZE) 
		{
			/* floatlen()'s *return value* is i+1 -- one more than the
			   trimmed string it just wrote into LINEA (e.g. it returns
			   5 for "3.08", which is 4 chars) -- so measure the actual
			   fractional length off LINEA itself rather than off the
			   return value, or dpmax (and every column's decimal count)
			   ends up one digit too wide. */
			floatlen(dm,-1,&dpd,&ep);
			i = (short)strlen((char *)LINEA);
			if ((i - dpd) > dpmax) dpmax = i - dpd;
			if (dpd > wmax) wmax = dpd;
			if (ep) use_e = 1;
		} else {

			i = 1;
			if (m < 0L) 
			{
				i = 2;
				m = 0-m;
			}
			while (m) 
			{
				m = m/10L;
				i++;
			}
		}
		if (i > w) w = i;
	}

	dp.cp = DATA_AD(ve);

	if (use_e) 
	{
		w = wmax + dpmax + 5;
		sprintf(ffmt,"%%%d.%de",w,dpmax);	/* construct maximized formatting string (E notation) */
	} else sprintf(ffmt,"%%%d.%df",wmax+dpmax+1,dpmax);	/* construct maximized formatting string (non-E notation) */
}

	rc = 0;
	if (rank) lc = (short)ve->ve_rho[0];
	else lc = 1;
	for (t=0L;t<l;t++) 
	{
		if ((ve->ve_vtype | VARIABLE_BIT) == QUOT_CODE) 
		{
			i = MIN(lc,LINE_SIZE-1);
			lc -= i;
			t += i - 1;
			rc += i;
			vout((UCHAR *)dp.cp,0,i);
			dp.cp += i;
		} else {
			switch (bm) {
				case 1:
					m = (long)(signed char)*dp.cp++;
					break;
				case 2:
					m = (long)*dp.sp++;
					break;
				case 4:
					m = *dp.lp++;
					break;
				case FLOATSIZE:
					dm = *dp.dp++;
					break;
			}
			if (rank > 1) 
			{
				if (OUT_BASE == 16) sprintf((char *)LINEA,"%0*x",w,(unsigned int)m);
				else {
					if (bm == FLOATSIZE) 
					{
						// floatlen(dm,w,&dpd,&ep);   		// (original way) 
						sprintf((char *)LINEA,ffmt,dm); 	// (first alternate) 

// this section attempts to use a call to floatlen (must enable above) to figure if it's an E notation
// value or not, and if it is, does it's own munging of the format.  Difficult to match up with non-E
// mixed formatting, so it doesn't work all that great...
//						if (ep)	   /* or use use_e to normalize the entire array */

						if (use_e)
						{
						/* unsigned char *sp; */

							sprintf(ffmt,"%%%d.%de",w,dpmax);	// construct normalized format string (E notation) 
							sprintf((char *)LINEA,ffmt,dm);

							/* convert lowercase E to uppercase, & remove leading zeros in E value */
							convert_numstring(LINEA,w);

//						} else {
//							sprintf(ffmt,"%%%d.%df",w,dpmax);	/* normalized non-E */
//							sprintf((char *)LINEA,ffmt,dm);
						}
					} else
					sprintf((char *)LINEA,"%*d",w,(int)m);
				}
			} else {
				if (OUT_BASE == 16) sprintf((char *)LINEA,"%x",(unsigned int)m);
				else {
					if (bm == FLOATSIZE) floatlen(dm,-1,&dpd,&ep);
					else
					sprintf((char *)LINEA,"%d",(int)m);
				}
				if (lc != 1) strcat((char *)LINEA," ");
			}
			rc++;
			lc--;

			if (NEGSIGN != '-')
			{
				cp = LINEA;
				while (*cp) 
				{
					if (*cp == '-') *cp = NEGSIGN;
					cp++;
				}
			}
			
			vout(LINEA,1,strlen((char *)LINEA));
			if (icheck++ > 500)
			{
				check_for_interrupt(0);	/* give an interrupt a chance */
				icheck = 0;
			}
		}
		if (!lc) 
		{
			j = 1;
			if (rank) 
			{
				m = ve->ve_rho[0]; 
				lc = (short)m;
				for (i=1;i<rank;i++) 
				{
					m *= ve->ve_rho[i];
					if (!(rc % m)) j++;
				}
			}
			if (rc != l) for (i=0;i<j;i++) NL();
		}
		if (ABORTSIG) return;
	}
}
