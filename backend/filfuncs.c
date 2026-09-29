/*
	filefuncs.c
*/

#define FILEFUNCS_C 1

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"
#include "externs.h"

#define MAX_FILES 10

extern short getdlist();
extern char *get_first_dir();
extern char *get_next_dir();

/* File-local macro: cast ws_off_t workspace offset to struct ValEnt* */
#define WSVAL_PTR(off) ((struct ValEnt *)WS_PTR(off))

UCHAR file_fnames[] = {
	'F','O','P','E','N',0,
	'F','C','L','O','S','E',0,
	'F','R','E','A','D',0,
	'F','N','U','M','S',0,
	'F','S','E','E','K',0,
	'F','T','E','L','L',0,
	'F','W','R','I','T','E',0,
	'F','C','R','E','A','T','E',0,
	'F','D','I','R',0,		/* FDIR */
	'F','N','A','M','E',0,	/* FNAME */
	'F','D','E','L',0,		/* FDEL */
	'F','E','O','F',0,		/* FEOF */
	'F','A','P','P','E','N','D',0,	/* FAPPEND */
	'F','F','L','U','S','H',0,	/* FFLUSH */
	0,0,0
};

UCHAR file_ftypes[] = {
	DYAD,	/* 0, FOPEN */
	MONAD,	/* 1, FCLOSE */
	DYAD,	/* 2, FREAD */
	NILAD,	/* 3, FNUMS */
	DYAD,	/* 4, FSEEK */
	MONAD,	/* 5, FTELL */
	DYAD,	/* 6, FWRITE */
	DYAD,	/* 7, FCREATE */
	MONAD,	/* 8, FDIR */
	DYAD,	/* 9, FNAME */
	MONAD,	/* 10,FDEL */
	MONAD,	/* 11,FEOF */
	DYAD,	/* 12,FAPPEND */
	MONAD,	/* 13,FFLUSH */
};

short f_opened = 0;
int32_t f_nums[MAX_FILES];		/* was long[] -- must match push_val() int32_t* arg */
FILE *f_fds[MAX_FILES];

FILE *
get_filep(fi)
int32_t fi;					/* was long */
{
short j;

	j = f_opened;
	while (j--) {
		if (f_nums[j] == fi) {
			return(f_fds[j]);
		}
	}
	return((FILE *)0);
}

short
f_open(le,re,m)
struct ValEnt *re,*le;
char *m;
{
char *cp,*tp;
short i;
int32_t acc,fnum;

	acc = 0;
	if (f_opened == MAX_FILES) return(SET_ERR(ERR_FTOVF));
	tp = (char *)LSCRATCH;
	if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
	fnum = (int32_t)get_data(le,0);
	if (!get_filep(fnum)) {
		f_nums[f_opened] = fnum;
		cp = DATA_AD(re);
		if (re->ve_length >= LINE_SIZE) return(SET_ERR(ERR_LENGTH));	/* check before narrowing to short */
		i = (short)re->ve_length;
		memcpy(tp,cp,i);
		tp[i] = '\0';
		f_fds[f_opened] = fopen(tp,m);
		if (f_fds[f_opened]) {
			f_opened++;
			acc = 1;
		}
	}
	stack_pop(2);
	push_val(1,0,&acc);
	return(0);
}

short
f_close(re)
struct ValEnt *re;
{
short i;
int32_t acc;

	if (!SBOUNDS(re)) return(SET_ERR(ERR_FILE));
	acc = get_data(re,0);
	i = f_opened;
	while (i) {
		i--;
		if (acc == f_nums[i]) {
			f_opened--;
			fclose(f_fds[i]);
			f_nums[i] = f_nums[f_opened];
			f_fds[i] = f_fds[f_opened];
			stack_pop(1);
			acc = 1;
			push_val(1,0,&acc);
			return(0);
		}
	}
	return(SET_ERR(ERR_FILE));
}

short
f_seek(le,re)
struct ValEnt *le,*re;
{
int32_t f,pos;
FILE *fp;

	if (!SBOUNDS(re) || !SBOUNDS(le)) return(SET_ERR(ERR_FILE));
	f = get_data(le,0);
	fp = get_filep(f);
	if (!fp) return(SET_ERR(ERR_FILE));
	pos = get_data(re,0);
	if (pos < 0L) fseek(fp,pos + 1L,2);
	else fseek(fp,pos,0);
	pos = (int32_t)ftell(fp);
	stack_pop(2);
	push_val(1,0,&pos);
	return(0);
}

short
f_ftell(re)
struct ValEnt *re;
{
int32_t lscratch[128];		/* local staging buffer: ftell() results narrowed to int32_t */
int32_t acc;
short i;
FILE *fp;

	if (!SBOUNDS(re)) return(0);
	/* one lscratch entry per file number -- more than it holds is an
	   error rather than a stack overrun (MAX_FILES is far smaller) */
	if (SBOUNDS(re) > (long)(sizeof(lscratch) / sizeof(lscratch[0])))
		return(SET_ERR(ERR_LENGTH));
	i = 0;
	while (SBOUNDS(re) > i) {
		lscratch[i] = -1;
		acc = get_data(re,i);
		fp = get_filep((int32_t)acc);
		if (fp) lscratch[i] = (int32_t)ftell(fp);
		i++;
	}
	stack_pop(1);
	push_val(i,1,lscratch);
	return(0);
}

short
f_write(le,re)
struct ValEnt *le,*re;
{
int32_t acc;
int32_t l;
FILE *fp;
char *cp;

	if (!SBOUNDS(re) || !SBOUNDS(le)) return(SET_ERR(ERR_FILE));
	acc = (int32_t)get_data(le,0);
	fp = get_filep(acc);
	if (!fp) return(SET_ERR(ERR_FILE));
	l = 1;
	if (re->ve_rank) l = re->ve_rho[0];
	cp = DATA_AD(re);
	acc = (int32_t)fwrite(cp,l,1,fp);
	if (ferror(fp))
	{
		clearerr(fp);
		acc = 0;
	}
	stack_pop(2);
	push_val(1,0,&acc);
	return(0);
}

short
f_read(le,re)
struct ValEnt *le,*re;
{
struct ValEnt *te;
int32_t acc;
int32_t fi;
short i,k;
char *cp,*dp;
FILE *fp;

	te = (struct ValEnt *)0;
	if (!SBOUNDS(re)) acc = -1L;
	else acc = get_data(re,0);		/* data length */
	/* a length of -1 means read a line; any other negative length would become an
	   enormous unsigned byte count for fread() below */
	if (acc < -1L) return(SET_ERR(ERR_DOMAIN));

	cp = (char *)LSCRATCH;

	i = 0;
	if (SBOUNDS(le) > i) {	/* could be while, if data can be handled */
		fi = get_data(le,i);		
		fp = get_filep((int32_t)fi);
		if (fp) {
			if (acc == -1L) {
				if (fgets(cp,LINE_SIZE,fp)) acc = (long)strlen(cp);
				while (acc > 0 && (cp[acc-1] == 10 || cp[acc-1] == 13)) acc--;
			} else {
				if (acc <= LINE_SIZE) {
					acc = (long)fread(cp,1,(unsigned int)acc,fp);
				} else {
					te = make_temp(QUOT_CODE,1,acc);
					if (!te) return(SET_ERR(ERR_WSFULL));
					re = WSVAL_PTR(*VRIGHT);
					le = WSVAL_PTR(*VLEFT);
					cp = DATA_AD(te);
					acc = (long)fread(cp,1,(unsigned int)acc,fp);
					if (acc > -1) te->ve_rho[0] = (int32_t)acc;
					else {
						stack_pop(1);
						te = (struct ValEnt *)0;
					}
				}
			}
			if (acc < 0L) acc = 0L;
			k = 1;
			if (!te) {
				te = make_temp(QUOT_CODE,k,acc);
				if (!te) return(SET_ERR(ERR_WSFULL));
				re = WSVAL_PTR(*VRIGHT);
				le = WSVAL_PTR(*VLEFT);
				dp = DATA_AD(te);
				memcpy(dp,cp,(int)acc);
			}
		}
		i++;
	}
	if (!te) return(SET_ERR(ERR_FILE));
	stack_pop(3);
	temp_stack(te);
	return(0);
}

short
f_feof(re)
struct ValEnt *re;
{
int32_t acc;
FILE *fp;

	if (!SBOUNDS(re)) return(SET_ERR(ERR_FILE));
	fp = get_filep((int32_t)get_data(re,0));
	if (!fp) return(SET_ERR(ERR_FILE));
	acc = feof(fp) ? 1L : 0L;
	stack_pop(1);
	push_val(1,0,&acc);
	return(0);
}

short
f_flush(re)
struct ValEnt *re;
{
int32_t acc;
FILE *fp;

	if (!SBOUNDS(re)) return(SET_ERR(ERR_FILE));
	fp = get_filep((int32_t)get_data(re,0));
	if (!fp) return(SET_ERR(ERR_FILE));
	acc = (fflush(fp) == 0) ? 1L : 0L;
	stack_pop(1);
	push_val(1,0,&acc);
	return(0);
}

/* get directory */
short
f_fdir(re)
struct ValEnt *re;
{
struct ValEnt *te;
char *tp,*cp,*vp;
short b;
short mxwid,i,count;
char *ps;

/* copy string into area where it can be terminated */
	b = 8192;
	tp = (char *)malloc((int)b);
	if (!tp) return(SET_ERR(ERR_WSFULL));
	cp = DATA_AD(re);
	if (re->ve_length >= b) {	/* check before narrowing to short */
		free(tp);
		return(SET_ERR(ERR_LENGTH));
	}
	i = (short)re->ve_length;
	strncpy(tp,cp,(int)i);
	tp[i] = '\0';
	
	count = mxwid = 0;
	cp = tp;
	ps = get_first_dir(tp);
	while (ps) 
	{
		if (ABORTSIG) 
		{
			free(tp);
			cleanup_dir();
			return(SET_ERR(ERR_INTERRUPT));
		}
		i = 1 + (short)strlen(ps);	/* count terminator */
		if (i > b) 
		{
			free(tp);
			cleanup_dir();
			return(SET_ERR(ERR_WSFULL));		/* more than 8K of directory */
		}
		strcpy(cp,ps);
		cp += i;
		b -= i;
		count++;
		if (mxwid < i) mxwid = i;
		ps = get_next_dir();
	}
	cleanup_dir();
	if (count) mxwid--;	/* only valid to remove the terminator-count if something was measured -- empty dir stays 0 0, not 0 ¯1 */

	te = make_temp(QUOT_CODE,2,(long)count*mxwid);
	if (!te) 
	{
		free(tp);
		return(SET_ERR(ERR_WSFULL));
	}
	te->ve_rho[0] = (int32_t)mxwid;	/* was (long) cast -- ve_rho[] is int32_t[] */
	te->ve_rho[1] = (int32_t)count;
	vp = DATA_AD(te);
	cp = tp;
	while (count--) 
	{
		for (b=0;b<mxwid;b++) vp[b] = ' ';		/* pad with spaces */
		b = (short)strlen(cp);
		memcpy(vp,cp,b);		/* copy without terminator */
		cp += b + 1;			/* skip to next */
		vp += mxwid;			/* in output as well */
	}
	free(tp);
	swapdrop();
	return(0);
}

short
file_funcs(f,le,re)
short f;
struct ValEnt *le,*re;
{
int32_t acc;
short b;
char *cp,*cp2;

	switch (f) {
		case 0:	/* fopen */
			return(f_open(le,re,"r+b"));	
		case 1:	/* fclose */
			return(f_close(re));
		case 2:	/* fread */
			return(f_read(le,re));
		case 3:	/* fnums */
			push_val(f_opened,1,f_nums);	/* f_nums is now int32_t[] */
			return(0);
		case 4:	/* fseek */
			return(f_seek(le,re));
		case 5:	/* ftell */
			return(f_ftell(re));
		case 6:	/* fwrite */	
			return(f_write(le,re));
		case 7:	/* fcreate */	
			return(f_open(le,re,"wb"));
		case 8:	/* fdir */
			return(f_fdir(re));
		case 9:	/* fname */
		{
			char namebuf1[PATHBUF_SIZE], namebuf2[PATHBUF_SIZE];

			cp2 = DATA_AD(le);
			if (SBOUNDS(le) >= PATHBUF_SIZE) return(SET_ERR(ERR_LENGTH));	/* check before narrowing to short */
			b = (short)SBOUNDS(le);
			strncpy(namebuf1,cp2,b);
			namebuf1[b] = '\0';

			cp2 = DATA_AD(re);
			if (SBOUNDS(re) >= PATHBUF_SIZE) return(SET_ERR(ERR_LENGTH));	/* check before narrowing to short */
			b = (short)SBOUNDS(re);
			strncpy(namebuf2,cp2,b);
			namebuf2[b] = '\0';

			acc = (int32_t)sys_rename(namebuf1,namebuf2);
			stack_pop(2);
			break;
		}
		case 10:	/* fdel */
			cp2 = (char *)LSCRATCH;
			cp = DATA_AD(re);
			acc = (int32_t)re->ve_length;
			if (acc >= LINE_SIZE) return(SET_ERR(ERR_LENGTH));
			strncpy(cp2,cp,(int)acc);
			cp2[acc] = '\0';
			acc = (int32_t)sys_fdelete(cp2);
			stack_pop(1);
			break;
		case 11:	/* feof */
			return(f_feof(re));
		case 12:	/* fappend */
			return(f_open(le,re,"ab"));
		case 13:	/* fflush */
			return(f_flush(re));
		case CLEANUP_FUNC:	/* module dependent cleanup */
			while (f_opened) fclose(f_fds[--f_opened]);
			return(0);
		default:
			return(SET_ERR(ERR_SYSTEM));
	}
	return(push_val(1,0,&acc));
}

