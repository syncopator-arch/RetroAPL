/*
	parser.h
*/

/* ABSOLUTES (external malloc'd variables via #AV / push_val) removed:
 * ME_MAL was never used in practice and ve_point can't hold a 64-bit
 * pointer.  DATA_AD always uses the workspace-relative offset form. */

#define FLOATSIZE 8
/* bit amount to shift a count to make it FLOATSIZE */
#define FLOATSHIFT 3


#define ISDIGIT(d) (((d) >= '0') && ((d) <= '9'))


/* floats used to determine if a float number will fit into an integer.  */

#define INTEGER_MAX 2147483647.0
#define INTEGER_MIN -2147483648.0

#define PATHBUF_SIZE 1028

/* use this in debugger to determine source of error */
#define TRACK_ERRS

/* trig functions (monadic and dyadic circle): dyop.c and monop.c keep
   them under #ifdef TRIG, one of the conditionals the ifdef cleanup
   deliberately kept (CLEANUP_STATUS.md). Its define was lost somewhere
   along the way -- found V70, when circle gave Syntax Error in both
   forms -- so it lives here now, beside TRACK_ERRS, where every backend
   file picks it up. tests/trig.apl covers it. */
#define TRIG

/* this was once intended to flag to move all globals into wspace */
/* left here as a reminder of the idea which may someday make sense with TURNK */
/* remaining loose ends are now removed from globals.h */
/* #define REENTRANT */

#include "depend.h"

#include <math.h>

#ifdef TRACK_ERRS
#define SET_ERR(e) err_track(e)
extern short err_track(short e);
#else
#define SET_ERR(e) e
#endif

#define is_varch(C) (((C >= var_lo && C <= var_hi) || (C >= '0' && C <= '9') || C == ALPH_CHAR) ? 1 : 0)

/* pointer to funcs, so that an array can be created */
typedef short (*PTF)();

#define DATA_AD(x) ((char *)(x) + (x)->ve_point)

#define CDATA_AD(x) (char *)(&(x)->ve_rho[(x)->ve_rank])
#define DATA_LEN(x) ((x)->ve_length)

#define PRINT(s) sys_print(s)
#define PCHAR(c) sys_pchar(c)
#define PUTS(s)  sys_puts(s)

#define CLEANUP_FUNC 127
#define SYSFUNC_SHIFT 5
#define SYSFUNC_MASK 0x1f

/* stack values */
#define STACK_VMASK 0xe0000000L
#define STACK_EMARK 1L	/* pushed function mark (shouldn't look like adrs) */
#define STACK_TEMP_VAL 2L
#define STACK_MARK 3L	/* index seperator */
#define INDEX_MARK 4L 	/* end of index mark */
#define INDEXA_MARK 5L	/* end of indexed assignment */
#define STACK_VARIABLE 0x80000000L
#define STACK_FLOATV 0x40000000L
#define STACK_FLINE 0x20000000L	/* fn line 0x2vvlllii */ 
#define STACK_FNHEAD 0x10000000L    /* fn headr  0x20000fff */

#define ILEN_SHIFT 0
#define ILEN_MASK 0xff
#define FNUM_SHIFT 0
#define FNUM_MASK 0xfff
#define FLINE_SHIFT 8
#define FLINE_MASK 0xfff
#define LVAR_SHIFT 20
#define LVAR_MASK 0xff
#define VNUM_MASK 0xfff

#define STACK_MARGIN 120
#define LINE_SIZE 1028	/* was 257 */
#define MAXSYM_CHARS 32
#define WSID_SIZE 22
#define MAX_RANK 68
#define NUM_FKEYS 12 /* also defined in globals.c */

/* NUM_FLOATVS is the number of allocatable "float"ing temp
   pointers provided */
#define NUM_FLOATVS 8 /* also defined in globals.c */
#define FLOATVS_MASK 7

/* error codes */
#define ERR_WSFULL 1
#define ERR_SYSTEM 2
#define ERR_SYNTAX 3
#define ERR_VALUE 4
#define ERR_DOMAIN 5
#define ERR_INDEX 6
#define ERR_RANK 7
#define ERR_COMMAND 8
#define ERR_INTERRUPT 9
#define ERR_STOVF 10
#define ERR_SI 11
#define ERR_LENGTH 12
#define ERR_KILL 13
#define MSG_WAS 14
#define MSG_IS 15
#define MSG_ON 16
#define MSG_OFF 17
#define ERR_FTOVF 18
#define ERR_FILE 19
#define ERR_FNOTF 20
#define ERR_WSNOTF 21
#define ERR_SAVE 22
#define ERR_WSD 23
#define ERR_WSLOAD 24
#define ERR_NOMEM 25
#define MSG_SAVED 26
#define MSG_HEX 27
#define MSG_DEC 28
#define ERR_TRACE 29
#define ERR_EXEC 30
#define ERR_FOPEN 31
#define ERR_NOFLOATV 32
#define ERR_BADFN 33
#define ERR_NUMOVF 34
#define ERR_IMGLOAD 35	/* WILOAD's (and formerly WIRELOAD's own, before
			   that call moved to plain -1/-2/-3 return codes
			   instead) image decode failure -- bad path,
			   corrupt file, or unsupported format. Was
			   ERR_DOMAIN for WILOAD originally; split out once
			   WIRELOAD needed to distinguish "file wouldn't
			   decode" from "file decoded fine but is the wrong
			   size," same reasoning ERR_WSLOAD already
			   established for ".ws wouldn't load" as its own
			   thing rather than overloading an existing code. */

/* mem entry flags */
#define ME_VAL 1	/* value */
#define ME_NAM 2	/* name string */
#define ME_FUNC0 3	/* function line 0 */
#define ME_FUNCN 4	/* function line n */
#define ME_ITMP 5	/* indexing temp */
#define ME_TMASK 0x7	/* mask out type */
#define ME_MAL 0x80	/* malloced bit */
#define ME_FTRACE 0x40	/* fn trace */
#define ME_LTRACE 0x20 /* stop before this line -- )STOP (was )LTRACE) */

/* )STEP modes -- step_mode in exec.c */
#define STEP_OFF 0
#define STEP_LINE 1
#define STEP_OP 2

/* symbol types */
#define SYM_UNDEF 0
#define SYM_VAR 0x40
#define SYM_FUN 0x80

/* compilation codes */
#define OP_CODE 0x80		/* an op if this bit is 0 */
#define CODE_MASK 0xe0		/* mask non-ops to identify further */
#define FUN_CODE 0x80		/* monadic or dyadic function */
#define NFUN_CODE 0xc0		/* niladic function */
#define FUN_MASK 0xf		/* mask value from fun op */
#define VAR_CODE 0xe0		/* variable (4 bits avail) */
#define SYS_CODE 0xd0		/* system fn/variable */
#define SYS_ASSIGN_BIT 0x01	/* in a compiled SYS_CODE byte (0xd1): the call
				   was written in the assignment form, ⎕IO←0.
				   parse.c's pass 2 emits it; exec.c's
				   lexec_run() then sets LAST_OPERATOR to
				   OP_ASS so end_exec() doesn't display the
				   result, as for any assignment. Test SYS_CODE
				   bytes with (c & 0xf0) == SYS_CODE, never
				   c == SYS_CODE. Added V77; see INVARIANTS.md
				   (MONAD_OPT section). */
#define CONST_CODE 0xa0		/* all consts are this when masked */
/* low nibble of all constants indicates number of bytes used */
#define BYTES_MASK 0xf
#define BCONST_CODE 0xa1	/* byte constant */
#define SCONST_CODE 0xa2	/* short constant */
#define ICONST_CODE 0xa4	/* integer constant */
#define FCONST_CODE 0xa8	/* float constant */
#define QUOT_CODE 0xb1		/* text constant */
#define STRUCT_CODE 0xb4	/* structure constant */
#define MONADIC 0x40		/* monadic op bit */
#define VARIABLE_BIT 0x80	/* if this bit clear, it's a not temp */

#define QUAD_CODE 0xfff		/* variable numbers for quad and qq */
#define QQUAD_CODE 0xffe		

/* individual operators.  All logic operators are less than OP_LOGIC */
#define OP_EQ 1
#define OP_NE 2
#define OP_GT 3
#define OP_GE 4
#define OP_LT 5
#define OP_LE 6
#define OP_AND 7
#define OP_OR 8
#define OP_NOT 8	/* monadic OR is used as NOT, same code */
#define OP_NAND 9
#define OP_NOR 10
#define OP_LOGIC 11

#define OP_DIV 11
#define OP_QU 12
#define OP_CE 13
#define OP_FL 14
#define OP_AB 15
#define OP_BAND 16
#define OP_BOR 17
#define OP_RS 18

/* Squad (dyadic only) -- indexed gather: L is a rank-2 N x rank(R) list of
 * coordinates into R, result is a rank-1 vector of N elements of R's vtype.
 * No monadic sense here -- monadic squad is a separate future expansion of
 * monadic iota (index generator), not part of this op. Takes slot 19 from
 * the three free slots (19/20/21) identified in the V29 dyadic-tilde/OP_NOT
 * investigation; 20 was subsequently claimed by OP_INR, and 21 has now been
 * claimed by OP_CATF below -- see OP_CATF's comment for why the dyadic-
 * tilde reservation turned out not to apply. No free slots remain in the
 * true legal range (see the note on OP_CATF/OP_COMPF below for why that
 * range tops out at 0x3f/63, not 0x7f as an earlier comment here claimed). */
#define OP_SQUAD 19

/* Generalized inner product (dyadic only): L f.g R, e.g. +.x matrix
 * product. The bare '.' doubles as both the decimal point and this glue
 * character -- parsenums()'s existing one-char digit lookahead already
 * disambiguates the two uses for free, so '.' is wired into op_table as a
 * plain entry (see parse.c) rather than getting its own char constant.
 * Compiled bytecode layout is [OP_INR][op2][op1] (mirrors OP_OUT's
 * [OP_OUT][op2], with one extra trailing byte for op1). Full derivation
 * in INNER_PRODUCT_PLAN_V1.md -- do not re-derive. Takes slot 20, one of
 * the two free slots (20/21) noted above; 21 has since been claimed too,
 * by OP_CATF below. */
#define OP_INR 20

/* common ops after here may result in result resizing */
/* used to determine if input temp can safely be used as output temp */
#define RESIZE_DESTRUCT 22

#define OP_LS 22
#define OP_ADD 23
#define OP_SUB 24
#define OP_MUL 25
#define OP_POW 26
#define OP_ENC 27	/* encode, L⊤R (representation) */
#define OP_DEC 28	/* decode, L⊥R (base value) */
/* OP_ENC/OP_DEC and CHAR_ENC/CHAR_DEC had their NAMES swapped (V66): the
   old names were backwards -- ⊤ (27) was called DEC and ⊥ (28) ENC, all
   the way through op_table and dyop.c's dispatch. The NUMBERS never
   moved with the glyphs, so compiled code, saved workspaces and .fn files
   are unaffected. */

/* above here are unusual ops */
#define SPECIAL_OP 30

/* Real APL Execute (⍎) and Format (⍕), entered via the Cap+Circle and
 * Decode+Circle overstrike glyphs added to mappings.hpp. These are NOT
 * related to OP_EXEC (46) below, which is the unshifted-'x'-key
 * system-command dispatcher (do_command()) and is unchanged/still in use.
 * OP_QX and OP_FMT share their core logic with the pre-existing quad
 * functions ⎕X and ⎕NTA (sysfuncs.c) via prep_exec_line()/fmt_number()
 * in monop.c, so those quad functions keep working for existing user
 * functions written against them. Slots 31/32 taken from the confirmed-
 * free 31/32/33 gap between SPECIAL_OP (30) and OP_OH (34). */
#define OP_QX 31	/* execute character vector as APL, returns a value (real Execute) */
#define OP_FMT 32	/* format numeric value as a character vector (real Format) */

/* Logarithm (⍟, circle-star, font byte 0xF0). Monadic ⍟R is the natural
 * log, dyadic L⍟R the log of R to base L; both always return float and
 * give Domain Error outside the real domain (R<=0; L<=0 or L=1). Takes
 * slot 33, the last of the 31/32/33 gap above. Replaces the old non-
 * standard 10○/11○ (log10/ln) circle functions, removed in V72 along
 * with ¯11○. This header defines nothing for 29 or 40 either, but parse.c
 * and the translate tables haven't been audited for those two, so check
 * before claiming them. */
#define OP_LOG 33	/* logarithm */

#define OP_OH 34	/* trig */
#define OP_DRP 35	/* drop */
#define OP_TAK 36	/* take */
#define OP_TRA 37	/* transpose */
#define OP_DNG 38	/* downgrade */
#define OP_UPG 39	/* upgrade */
#define OP_OUT 41	/* outer product */
#define OP_EXP 42   /* expand */
#define OP_COMP 43  /* compress */
#define OP_REDUCTION 43  /* compress is reduction if monadic */
#define OP_EXEC 46	/* execute command */
#define OP_RAV 47	/* ravel/concatenate */
#define OP_BRA 48	/* branch */
#define OP_QDAS 49	/* quad-assign */
#define OP_QTAS 50	/* quote-assign */
#define OP_VROT 51	/* vert rotate */
#define OP_HROT 52	/* horiz rotate */
#define OP_NOP 53	/* dyadic ';' treated as noop placeholder */
#define OP_INA 54	/* indexed assignment */
#define OP_IND 55	/* index */
#define OP_IOT 56
#define OP_RHO 57
#define OP_ASS 58

#define OP_RB 59
#define OP_LB 60
#define OP_RP 61
#define OP_LP 62

/* CORRECTED (was wrong): OP_ values must stay below 0x40 (MONADIC), NOT
 * merely below 0x80 (OP_CODE) as this comment previously claimed. Proof:
 * parse.c's compiler, on seeing a plain op in monadic position, does
 * `*cpt++ = op_table[i] | MONADIC;` (the `default:` case of parse.c's main
 * op-table switch) -- MONADIC is 0x40, so any OP_ value with that bit
 * already set (0x40 and above) is indistinguishable, once OR'd, from some
 * *other*, lower op value with MONADIC set on top of it (e.g. an OP_
 * value of 0x40 exactly is byte-identical to "op 0 | MONADIC"). This isn't
 * a theoretical concern specific to some hypothetical future op -- it
 * would have silently broken OP_CATF itself, which briefly sat at 64
 * (0x40) in an earlier pass through this file; caught before parse.c's
 * op_table entry was ever added, see OP_CATF's own comment below. 63
 * (0x3f) is therefore the true top of the legal range, and OP_COMPF (63)
 * already occupies it -- no headroom above 63 remains. */
#define OP_COMPF 63		/* compress, first axis (overstrike-entered ⌿) */

/* First-axis dyadic catenate ( L CATF R ), CHAR_CATF's op number. Plain
 * dyadic catenate (OP_RAV, do_drav() in dyop.c) joins along axis 0 (the
 * innermost/fastest-varying axis -- columns, for a rank-2 value); this is
 * the same operation joining along the outermost/slowest-varying axis
 * instead (rows, for a rank-2 value) -- implemented in dyop.c's
 * do_drav1(), named to match the do_compx1()/do_expand1() first-axis
 * naming convention already used for OP_COMPF/OP_EXPF above.
 *
 * Originally assigned 64, reasoning (wrongly) that anything below 0x80
 * was fair game -- see the corrected OP_COMPF comment just above for why
 * that's false and 64 (0x40) specifically collides with the MONADIC bit.
 * Reassigned to 21, the one slot the OP_SQUAD/OP_INR comments already
 * identified as free within the *true* legal range (below 0x40). The
 * earlier concern about reserving 21 for a future dyadic-tilde/OP_NOT
 * feature no longer applies: both monadic and dyadic tilde are confirmed
 * working in the current build already, via existing machinery (monadic
 * NOT already piggybacks on OP_OR, see OP_NOT's comment above) -- so
 * nothing else is waiting on this slot, and 21 is genuinely free. This
 * was also, as it turns out, the *only* free slot below 0x40: every
 * other gap in the 1-63 range is already claimed (see the confirmed-free
 * audit note below, which predates this op and only found the since-
 * consumed 44/45). */
#define OP_CATF 21		/* catenate, first axis (comma overstruck with -) */

/* 44 confirmed free: audited all OP_ values 1-63 in this header, cross-
 * checked against dyop.c/parse.c for stray literal use, and confirmed
 * with the platform translate tables that 44/45 (0x2c/0x2d) appear only
 * there, not as reserved op numbers. */
#define OP_EXPF 44		/* expand, first axis (overstrike-entered ⍀) */

#define OP_EPS 45		/* membership (dyadic epsilon, L∊R) */

#define OP_MARK 255		/* mark for indexing */

/* extention characters */
#define ALPH_CHAR '_'  /* char also considered alpha */
#define CHAR_EQ 1
#define CHAR_NE 2
#define CHAR_GE 3
#define CHAR_LE 4
#define CHAR_LS 0xDA	/* left shift (was 5) */
#define CHAR_RS 0xD8	/* right shift (was 6) */
#define CHAR_POW 7	/* power */
#define CHAR_BRA 11 	/* -> branch */
#define CHAR_OUT 14	/* outer product */
#define CHAR_HROT 15	/* horiz rotate */
#define CHAR_UPG 16	/* upgrade */
#define CHAR_DNG 17	/* downgrade */
#define CHAR_TRA 18 	/* transpose */
#define CHAR_VROT 19	/* vert rotate */
#define CHAR_QTAS 20	/* quote quad assign */
#define CHAR_QDAS 21	/* quad assign */
#define CHAR_SYS 22	/* internal fn/var */
#define CHAR_BAND 23	/* binary and */
#define CHAR_BOR 24	/* binary or */
#define CHAR_FL 25	/* floor */
#define CHAR_CE 26	/* ceiling, !~ is floor */
#define CHAR_ENC 27	/* encode ⊤ -- see OP_ENC for the V66 name swap */
#define CHAR_DEC 28	/* decode ⊥ */

/* Execute (⍎) and Format (⍕). Values per the OSTab overstrike-composition
 * entries in mappings.hpp: {Cap, Circle} -> Execute, {Decode, Circle} ->
 * Format. Monadic only, same as the ⎕X/⎕NTA quad functions they mirror. */
#define CHAR_QX 0xE2	/* Execute -- Cap + Circle overstrike */
#define CHAR_FMT 0xEE	/* Format -- Decode + Circle overstrike */

/* Logarithm (⍟) -- Circle + Star overstrike; font byte per the
 * overstrike table, and kapl_unicode.c maps it to U+235F. */
#define CHAR_LOG 0xF0	/* logarithm, circle-star */

/* Overstrike-produced
 * byte for compress-first-axis (⌿); value per prior observation of the
 * overstrike table. Confirm against parse.c before relying on this. */
#define CHAR_COMPF 0xaf	/* compress, first axis (/ overstruck with -) */

/* Value per the platform overstrike/translate table (confirmed alongside
 * CHAR_COMPF's 0xaf). Wired into parse.c's op_table in this same pass. */
#define CHAR_EXPF 0xdc	/* expand, first axis (\ overstruck with -) */

/* First-axis concatenate (comma overstruck with -). Op number is OP_CATF
 * (see above, now 21 -- was briefly, wrongly, 64; see OP_CATF's comment),
 * implementation is dyop.c's do_drav1(). Wired into parse.c's op_table
 * this session, same as CHAR_COMPF/CHAR_EXPF/CHAR_EPS. */
#define CHAR_CATF 0xAC

/* Membership (∊). Dyadic only -- no monadic (enlist) sense in this
 * dialect. Value per the platform overstrike/translate table, same
 * source as CHAR_COMPF/CHAR_EXPF above. Wired into parse.c's op_table
 * in this same pass. */
#define CHAR_EPS 0xc5	/* membership, dyadic epsilon */

/* Squad (index). Dyadic only -- see OP_SQUAD in the op-number section for
 * the L/R shape contract. Overstrike-produced byte for the squashed-quad
 * glyph ([ overstruck with ]); value confirmed against mappings.hpp. */
#define CHAR_SQUAD 0xbe	/* squad, dyadic index */

/* NAND/NOR. Dyadic only, same logic-op family as AND/OR/BAND/BOR --
 * no monadic sense. Values per the platform overstrike/translate table
 * (0xb0 = NAND overstrike, 0xb9 = NOR overstrike), NOT YET independently
 * confirmed against that table the way CHAR_COMPF/CHAR_EXPF/CHAR_EPS
 * were -- verify before relying on this. Wired into parse.c's op_table
 * in this same pass; replaces the removed OP_XOR ('^') table entry. */
#define CHAR_NAND 0xb0	/* logical NAND */
#define CHAR_NOR 0xb9	/* logical NOR */

#define CHAR_AS '='	 /* assignment (0x3d) */
/* #define CHAR_AS 0xfb 	assignment */
/* #define CHAR_CE '~'	ceiling, !~ is floor */
#define CHAR_NG '-'
/* #define CHAR_QT '"'     now defined in depends.h */
#define CHAR_QD '#'	/* quad */
#define CHAR_QQ '$'	/* quote-quad */
#define CHAR_COMMENT '`'

/* internal function type codes */
#define NILAD 0
#define MONAD 1
#define DYAD 2
#define MONAD_OPT 3 /* accepts 0 or 1 right operand */
#define MONAD_OPT_NOASSIGN 4	/* MONAD_OPT whose argument is not a setting
				   (it picks what to report, e.g. ⎕WSIZE's window,
				   ⎕WNUMS's tag pattern), so it has no ⎕F←X form and
				   keeps the old ⎕F X form and its arity guessing
				   under every -q mode. Same as MONAD_OPT everywhere
				   else: parse.c and sys.c's do_sys() treat it so.
				   The type exists only in the *_ftypes[] tables,
				   never in compiled code (V77). */

/* Quad-function syntax mode (quad_syntax, parse.c), set from the front
   end's -q option before dll_startup(). Applies to MONAD_OPT functions
   only; ⎕F←X is accepted in every mode. */
#define QSYN_BOTH 0	/* ⎕F X and ⎕F←X both accepted (default) */
#define QSYN_WARN 1	/* both accepted; )ENTER notes each old-form use */
#define QSYN_NEW  2	/* only ⎕F←X: without ←, ⎕F takes no argument */
extern int quad_syntax;
extern short qsyn_fn, qsyn_line;	/* )ENTER's function symbol and line for
					   warn notes; qsyn_fn -1 = no notes */

/* parameter checking macros */
/* NOTE: ve_rho orders dimensions from least- to most-significant --
 * rho[0] is the innermost dimension (columns), not the dimension as
 * written first in the shape (see struct ValEnt above). For a rank<=1
 * vector this distinction doesn't matter (only one dimension exists),
 * which is why SBOUNDS works correctly for every rank<=1 caller in the
 * codebase to date. It is NOT a safe stand-in for "row count" or "total
 * element count" on a genuine rank>=2 value -- for that, use ve_length
 * (DATA_LEN) for total elements, and index ve_rho directly with its
 * least-significant-first ordering in mind for a specific dimension. */
#define SBOUNDS(ve) ((ve)->ve_rank ? (ve)->ve_rho[0] : 1L)

/*
 * Workspace offset type.
 * All pointers into the workspace are stored as 32-bit offsets relative
 * to the workspace base pointer (wspace).  This keeps structures the same
 * size on 32-bit and 64-bit platforms and makes saved workspaces portable.
 *
 * Use WS_PTR(offset) to convert an offset back to a real pointer.
 * Use WS_OFF(ptr)    to convert a real pointer to an offset.
 *
 * Pointers that may live OUTSIDE the workspace (symbol
 * table, stack) are handled separately and are NOT stored as offsets.
 */
typedef int32_t ws_off_t;

/* wspace is declared in mplmem.c; these macros need it visible */
#define WS_PTR(off)  ((struct MemEnt *)((char *)wspace + (off)))
#define WS_OFF(ptr)  ((ws_off_t)((char *)(ptr) - (char *)wspace))

/*
 * Workspace file format version. Bump this whenever struct WorkSpace's
 * layout changes in a way that consumes reserved space or otherwise
 * affects the raw on-disk blob. load_ws() rejects any file whose version
 * doesn't match what this build understands -- no silent best-effort
 * reads of a version we don't recognize.
 */
#define WS_FORMAT_VERSION 1L

/* note: duplicated in ValEnt */
struct MemEnt {
	ws_off_t me_reference;	/* ws offset of the slot that points back to us */
	int32_t  me_size;		/* size of this entry in bytes */
	UCHAR    me_flags;
	UCHAR    me_vtype;
	int16_t  me_rank;
};

struct ValEnt {
/* duplicate of MemEnt */
	ws_off_t ve_reference;	/* ws offset of the slot that points back to us */
	int32_t  ve_size;
	UCHAR    ve_flags;
	UCHAR    ve_vtype;
	int16_t  ve_rank;
/* end of MemEnt */
	int32_t  ve_point;		/* offset to data from start of ValEnt */
	int32_t  ve_length;		/* number of elements */
	
	/* Shape array, ordered from least- to most-significant dimension:
	 * rho[0] is the size of the innermost/fastest-varying dimension
	 * (columns), rho[1] the next one out (rows), rho[2] the one after
	 * that, and so on -- same idea as a positional number system
	 * ordering digits from the smallest place value up. E.g. for a
	 * value of shape 3 2⍴... (3 rows, 2 cols): ve_rank==2, ve_rho[0]==2
	 * (columns), ve_rho[1]==3 (rows). Confirmed via a real rank-2 value
	 * during the graphics WPOINTS work (see apl_graphics_context_V2.md)
	 * -- easy to misread rho[0] as "the first dimension as written" if
	 * you're expecting row-major order; it isn't, it's the last one. */
	int32_t  ve_rho[1];		/* shape array, columns first */
};

struct NameEnt {
	struct MemEnt MemEnt;
	short ne_length;
	UCHAR ne_data[2];
};

struct FuncEnt {
	struct MemEnt fe_MemEnt;
	ws_off_t      fe_NextLine;	/* ws offset to next function line MemEnt */
	UCHAR         fe_exec[4];
};

struct Func0Ent {
	struct MemEnt f0e_MemEnt;
	ws_off_t      f0e_Line1;	/* ws offset to first function line */
	short f0e_result;
	short f0e_left;
	short f0e_right;
	short f0e_locals[1];
};

union MultiP {
	char   *cp;
	short  *sp;
	int32_t *lp;			/* was long* -- now int32_t* to match ve_rho etc */
	double *dp;
};

#define SYMNAME_MAX 6

struct SymTable {
	ws_off_t value;			/* ws offset to ValEnt, or 0 if undefined */
	union {
		ws_off_t a;			/* ws offset to name MemEnt when name is long */
		UCHAR    c[4];		/* inline name storage when namelen <= SYMNAME_MAX */
	} name;
	UCHAR n2[2];			/* 2 more inline name bytes (total 6 = SYMNAME_MAX) */
	UCHAR type;
	UCHAR namelen;
};

struct WorkSpace {
	int32_t  water;			/* current high-water mark (bytes used) */
	int32_t  wssize;		/* total workspace size in bytes */
	int32_t  symtsize;		/* number of SymTable entries (for save/load verify) */
	ws_off_t curfunc;       /* replaces g_curfunc global */
	ws_off_t tmp_me;        /* replaces g_tmp_me global */
	ws_off_t tmp_ve;        /* replaces g_tmp_ve global */
	short    origin;		/* index origin (0 or 1) */
	short    bootfn;		/* function to auto-run on )LOAD (0=none) */
	short    caseflip;
	short    width;			/* )WIDTH setting -- was g_syswidth only, now persisted */
	short    tstamp[6];
	ws_off_t begin;			/* ws offset to first MemEnt */
	char     wsid[WSID_SIZE];
	char     WS[2];
	int32_t  digits;		/* )DIGITS / DIG setting -- consumed from reserved[8] below,
				   see WS_FORMAT_VERSION bump */
	UCHAR    floatdiv;		/* )INTDIV / ⎕INTDIV setting, stored INVERTED from
				   the user-facing polarity: 0 = integer dyadic divide
				   (this APL's original behavior -- what every .ws
				   saved before this field existed reads here, since
				   reserved[] was zeroed), 1 = dyadic divide always
				   returns float (classic APL; init_ws() sets this for
				   new/cleared workspaces). Read directly as
				   wspace->floatdiv (no global mirror, same as origin).
				   Single byte on purpose: no bswap_ws() entry needed.
				   Carved, with flags_spare[], from the old reserved[0]
				   -- struct size unchanged (SYMT sits at
				   wspace + sizeof(struct WorkSpace), so it must stay
				   unchanged), no WS_FORMAT_VERSION bump. */
	UCHAR    flags_spare[3];	/* zeroed, unused -- room for 3 more byte flags.
				   Zero must mean "original behavior" for any flag
				   put here, for the same compatibility reason; the
				   default for NEW workspaces is init_ws()'s call. */
	int32_t  reserved[6];	/* zeroed, unused -- room for future header fields
				   without disturbing this layout. Consume from the
				   front; keep sizeof(struct WorkSpace) unchanged,
				   or bump WS_FORMAT_VERSION if that can't be done. */
};

#include "globals.h"

