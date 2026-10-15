/*
	depend.h
*/

/* #define VERSION "3.1"	from 2.1->3.0, conversion to VC++ & Win32 DLL w/Delphi front end */
// 3.2 - added logic for quad-X and so that Quad will work with expressions
// 3.3 - fixed power function as 2*31 was due to dyop upsizing issues-- 2*31 had two 8 bit ints and need to
//       resize to float (or at least 4 bit int) and it would only upsize one level-- fixed to that it can
//       upsize to wherever it needs to be.   Also fixed FLOOR logic so that it would check neg mag as well as pos
//       fix upsizing index recalculation in dyop
// 3.4 - fix </ and >/ (reduction) which was flipped.  Also start on decode/encode...
// 3.5 - finished encode & decode, added #AV, adjusted )VERS, fixed )HELP and converted compress to replicate
// 4.0 - Linux/aarch64 port: removed Amiga/MSDOS/Windows/curses dependencies,
//       converted workspace pointers to 32-bit offsets, fixed 64-bit struct layouts
// 4.1 - version numbering consolidated into depend.h (backend + frontend)

// BACKEND_VERSION tracks this file's own change log above (the interpreter core).
// FRONTEND_VERSION tracks the Qt GUI (APLTerminalWidget.cpp / main.cpp) and is
// bumped independently -- it does not need to move in lockstep with the backend.
// Composite title shown to the user is "RetroAPL <BACKEND_VERSION>.<FRONTEND_VERSION>".
// frontend 2.2 and backend 4.2 adds graphics functionality
// backend 4.3 adds ifdef cleanup for float & turnk
// backend 4.5 adds monadic table: ⍪
// backend 4.7 adds sequencer and ^ glyph corrections
#define BACKEND_VERSION  "4.7"
#define FRONTEND_VERSION "2.4"
#define APL_PRODUCT_NAME "retroapl"

#define VERSION BACKEND_VERSION   // legacy alias -- kept in case anything else references VERSION directly

#define BUILD __DATE__ " " __TIME__

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>

#define mem_alloc(l) malloc((unsigned int)l)

#define NL_CHAR    10
#define OTHER_CHAR 13
#define WS_POSTFIX "*.ws"
#define FN_POSTFIX "*.fn"
#define M_ioctl(a,b,c) ioctl(a,b,c)
#define QUAD_CHR '#'
#define CHAR_QT  '\''
#define MAX_NAME 31

#ifndef MIN
#define MIN(a,b) (a < b ? a : b)
#endif
#ifndef MAX
#define MAX(a,b) (a < b ? b : a)
#endif
#ifndef ABS
#define ABS(a) (a < 0 ? 0-a : a)
#endif

typedef unsigned char UCHAR;

#ifndef REG
#define REG
#endif
