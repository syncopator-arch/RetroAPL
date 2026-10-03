/*
	lineproc.c
*/

#define LINEPROC_C 1

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"
#include "externs.h"
#include "platform.h"

UCHAR var_lo = 'A';
UCHAR var_hi = 'Z';
UCHAR alph_lo = 'a';
UCHAR alph_hi = 'z';

/*
	getin() -- read one line of quad or quote-quad input into b (a
	LINE_SIZE buffer), as font-index bytes: the front end's registered
	getin_fn, through alt_getin(). Its old keystroke loop, and the
	ASCII-digraph translator input_proc()/input_line() that went with
	it, served only the stub_test build, removed with it.
*/
short
getin(b)
UCHAR *b;
{
	return(alt_getin(b));
}
