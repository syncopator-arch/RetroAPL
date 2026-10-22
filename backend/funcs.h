// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	funcs.h
*/

/* max of 8 funcs subsets can be defined at once */
/* note that SYSFUNCS and FILEFUNCS are hardcoded in, no #ifdefs so they can't be turned off */
/* though that could be changed if a TURNK version didn't need one */
#define SYSFUNCS
#define FILEFUNCS
#define GRAPHFUNCS  

extern UCHAR linea[LINE_SIZE];
extern UCHAR lineb[LINE_SIZE];
/* curfunc and returnval accessed via CURFUNC/RETURNVAL macros in globals.h */
extern short debugflag;
extern short pop_fn();
extern short last_operator;
extern short Error_Code;
/* vleft, vright, vtemp are ws_off_t* -- accessed via VLEFT/VRIGHT/VTEMP macros */
extern ws_off_t *vleft,*vright,*vtemp;
extern long totscan;
extern struct ValEnt *dup_val();
extern ws_off_t *get_vparm();
extern long compsize();
extern struct ValEnt *make_temp();
extern short stack_pop();
extern short upsize();
extern long get_vect();
extern short do_dhrot();
extern short fnline;
extern short timebase;
extern short sys_rename();
extern int globargc;
extern char **globargv;

/* index and count int32_t since V99 (were short: past 32767 they
   wrapped negative); keep in step with sys.c, externs.h and monop.c */
extern int32_t get_data(struct ValEnt *ve, int32_t n);
extern double get_fdata(struct ValEnt *ve, int32_t n);
extern short push_val(int32_t c,short r,int32_t *l);
extern short push_str();

