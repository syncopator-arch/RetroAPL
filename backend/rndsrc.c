// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
    randomization functions

    One generator for every platform: the Lehmer ("minimal standard")
    generator classic APL used behind quad-RL -- state <- 16807 * state mod
    (2^31 - 1). Its whole state is one integer in 1..2147483646, which is
    exactly what quad-RL reports and sets (sysfuncs.c case 6), so a program
    can bookmark the sequence (S<-quad-RL) and replay from that point later
    (quad-RL S). Period 2^31 - 2.

    Replaces platform_random(), which was random() on Linux (seedable,
    but glibc's own sequence) and BCryptGenRandom on Windows (not
    seedable at all) -- neither could give a seed a meaning, or the same
    sequence on both builds. platform_get_seed() is still used, once,
    for the unpredictable starting state a session gets when it never
    sets quad-RL. platform_seed()/platform_random()/PLATFORM_RAND_MAX were
    removed from the platform files along with this change.

    Session-global, deliberately not stored in the workspace (decided in
    conversation): )LOAD/)CLEAR/)SAVE don't touch it. Should that ever
    change, a zero in the header's reserved block could mean "not set",
    since 0 is never a valid state.
*/
#define RNDSRC_C 1

#include <stdlib.h>
#include <stdint.h>
#include "parse.h"
#include "externs.h"

#define RL_MOD	2147483647UL	/* 2^31 - 1, prime */
#define RL_MUL	16807UL		/* 7^5, classic APL's multiplier */
#define RL_SPAN	((unsigned long)RND_MAXN)	/* number of distinct states,
					   1..RL_MOD-1, i.e. RL_MOD - 1;
					   named in externs.h for callers */

static uint32_t rl_state = 0;	/* 0 = not yet seeded */

/* unpredictable starting state, mapped into 1..RL_MOD-1 */
static void rl_seed_entropy(void)
{
    rl_state = (uint32_t)((unsigned long)platform_get_seed() % RL_SPAN) + 1U;
}

/* advance one step, return the new state (1..RL_MOD-1). 64-bit product:
   16807 * (2^31-2) fits comfortably, so no Schrage's-method juggling. */
unsigned long rndcore(void)
{
    if (!rl_state) rl_seed_entropy();
    rl_state = (uint32_t)(((uint64_t)rl_state * RL_MUL) % RL_MOD);
    return (unsigned long)rl_state;
}

/* quad-RL, bare: the current state -- the value the next draw advances
   from. Seeds first if nothing has yet, so the very first bare quad-RL of a
   session already reports a real, replayable state. */
int32_t rl_get(void)
{
    if (!rl_state) rl_seed_entropy();
    return (int32_t)rl_state;
}

/* quad-RL s: s in 1..RL_MOD-1 sets the state; 0 reseeds unpredictably.
   Returns 0, changing nothing, for anything else (caller raises
   ERR_DOMAIN) -- the range lives here so sysfuncs.c doesn't need to
   know the modulus. */
short rl_set(long s)
{
    if (s < 0L || (unsigned long)s >= RL_MOD) return 0;
    if (s == 0L) rl_seed_entropy();
    else rl_state = (uint32_t)s;
    return 1;
}

/* uniform integer in 0..n-1, by rejection sampling over rndcore()'s
   RL_SPAN equally likely values (shifted to 0..RL_SPAN-1).

   n larger than RL_SPAN is ERR_DOMAIN: there aren't enough distinct
   values to cover it. Before this, any n over 2^31 left limit at 0 and
   looped forever (?3E9 hung) -- an error is the fix, not a behavior
   loss. */
unsigned long myrnd(n)
unsigned long n;
{
    unsigned long raw, limit;

    if (n < 1 || n > RL_SPAN)
    {
        ERROR_CODE = SET_ERR(ERR_DOMAIN);
        return (unsigned long)-1;
    }

    limit = RL_SPAN - (RL_SPAN % n);
    do { raw = rndcore() - 1UL; } while (raw >= limit);
    return raw % n;
}
