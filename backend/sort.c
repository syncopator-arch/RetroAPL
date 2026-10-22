// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	shellsort.  Was heapsort, but heapsort rotates items that are
	identical, so a shellsort is being used instead.

	sorts 'n' items pointed to by ra, along with indexes pointed
	to by rb.
*/


#define SORT_C 1

#include "parse.h"
#include "mplmem.h"

/*
	PERFORMANCE:
	calls to this routine represent a significant amount of
	overhead of sort, as this is called thousands of times
*/
char *
get_sortd(pp,s,l)
char *pp;
short s;
long l;
{
union MultiP p;

	p.cp = pp;

	l--;		/* origin 1 indexing */
	l *= SORTSIZE;
	switch (s) {
		case 1:
			p.cp += l;
			break;
		case 2:
			p.sp += l;
			break;
		case 4:
			p.lp += l;
			break;
		case FLOATSIZE:
			p.dp += l;
			break;
	}
	return(p.cp);
}

/*
	PERFORMANCE:
	calls to this routine represent a significant amount of
	overhead of sort, as this is called thousands of times
*/
short
sort_cmp(s,v1,v2)
REG short s;
char *v1,*v2;
{
REG long i;
union MultiP va,vb;

	va.cp = v1;
	vb.cp = v2;

	i = SORTSIZE;
	while (i--) {
		switch (s) {
			case 1:
				if ((signed char)*va.cp < (signed char)*vb.cp) return(1);
				if ((signed char)*va.cp++ != (signed char)*vb.cp++) return(0);
				break;
			case 2:
				if (*va.sp < *vb.sp) return(1);
				if (*va.sp++ != *vb.sp++) return(0);
				break;
			case 4:
				if (*va.lp < *vb.lp) return(1);
				if (*va.lp++ != *vb.lp++) return(0);
				break;
			case FLOATSIZE:
				if (*va.dp < *vb.dp) return(1);
				if (*va.dp++ != *vb.dp++) return(0);
		}
	}
	return(0);
}

/* 
	latest approach "unrolls the loop" by determining INDSIZE
	early and implementing the majority of the loop seperately for
	each size.  Probably could make a FASTSORT flag that would
	select sort speed over code size here, by merging the old
	and new code.  Since INDSIZE can only be either byte or 
	short, this unrolling only has to provide for two sizes.
	This produced a 1/2 elapsed time in a 5000 integer sort
*/
void
heapsort(n,rap,rbp)
long n;
char *rap,*rbp;
{
long i,j,k,w,org;
char *v1;
short *rbps;
long ival;

	rbps = (short *)rbp;
	org = (long)wspace->origin;

	if (INDSIZE == 1) {		/* will be either 1 or 2 */
		for (i=2;i<=n;i++) {
			w = (long)*(rbp + (i - 1)) - org;
			v1 = get_sortd(rap,SRCSIZE,w + 1L);
			j = i;
			for (;;) {
				ival = ((long)*(rbp + (j - 2)) - org) + 1L;
				if (SORTDIR) {
					k = sort_cmp(SRCSIZE,get_sortd(rap,SRCSIZE,ival),v1);
				} else {
					k = sort_cmp(SRCSIZE,v1,get_sortd(rap,SRCSIZE,ival)); 
				}
				if (j < 2 || !k) break;
				*(rbp + j - 1) = *(rbp + j - 2);
				j--;
			}
			*(rbp + j - 1) = (char)(w + org);
		}
	} else {
		for (i=2;i<=n;i++) {
			w = (long)*(rbps + (i - 1)) - org;
			v1 = get_sortd(rap,SRCSIZE,w + 1L);
			j = i;
			for (;;) {
				ival = ((long)*(rbps + (j - 2)) - org) + 1L;
				if (SORTDIR) {
					k = sort_cmp(SRCSIZE,get_sortd(rap,SRCSIZE,ival),v1);
				} else {
					k = sort_cmp(SRCSIZE,v1,get_sortd(rap,SRCSIZE,ival)); 
				}
				if (j < 2 || !k) break;
				*(rbps + j - 1) = *(rbps + j - 2);
				j--;
			}
			*(rbps + j - 1) = (short)(w + org);
		}
	}
}

