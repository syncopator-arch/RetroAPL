// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
    interfac.c  -- interface to system dependent routines
*/

#define INTERFAC_C 1

#include "parse.h"
#include "platform.h"
#include "externs.h"

extern PTF all_funcs[];
extern UCHAR *all_ftypes[];
extern void die(short f);

#ifdef TRACK_ERRS
long err_counts = 0L;

short
err_track(short e)
{
    if (e == ERR_SYNTAX) err_counts++;
    return(e);
}
#endif

void
proc_abort(int n)
{
    ABORTSIG++;
    if (ABORTSIG > 3)
    {
        MULTI = 0;
        KILLME = 1;
        NL();
        PRINT(" * * * Killed * * *");
        NL();
    }
    if (KILLME) die(1);
    setabort();
}
