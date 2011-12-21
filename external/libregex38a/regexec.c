/*
 * the outer shell of cub_regexec()
 *
 * This file includes engine.c *twice*, after muchos fiddling with the
 * macros that code uses.  This lets the same code operate on two different
 * representations for state sets.
 */
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

#include "utils.h"
#include "include/regex38a.h"
#include "regex2.h"
#include "regmem.ih"

static int nope = 0;		/* for use in asserts; shuts lint up */

/* macros for manipulating states, small version */
#define	states	unsigned
#define	states1	unsigned	/* for later use in cub_regexec() decision */
#define	CLEAR(v)	((v) = 0)
#define	SET0(v, n)	((v) &= ~((unsigned)1 << (n)))
#define	SET1(v, n)	((v) |= (unsigned)1 << (n))
#define	ISSET(v, n)	((v) & ((unsigned)1 << (n)))
#define	ASSIGN(d, s)	((d) = (s))
#define	EQ(a, b)	((a) == (b))
#define	STATEVARS	int dummy	/* dummy version */
#define	STATESETUP(m, n)	/* nothing */
#define	STATETEARDOWN(m)	/* nothing */
#define	SETUP(v)	((v) = 0)
#define	onestate	unsigned
#define	INIT(o, n)	((o) = (unsigned)1 << (n))
#define	INC(o)	((o) <<= 1)
#define	ISSTATEIN(v, o)	((v) & (o))
/* some abbreviations; note that some of these know variable names! */
/* do "if I'm here, I can also be there" etc without branches */
#define	FWD(dst, src, n)	((dst) |= ((unsigned)(src)&(here)) << (n))
#define	BACK(dst, src, n)	((dst) |= ((unsigned)(src)&(here)) >> (n))
#define	ISSETBACK(v, n)	((v) & ((unsigned)here >> (n)))
/* function names */
#define SNAMES			/* engine.c looks after details */

#include "engine.c"

/* now undo things */
#undef	states
#undef	CLEAR
#undef	SET0
#undef	SET1
#undef	ISSET
#undef	ASSIGN
#undef	EQ
#undef	STATEVARS
#undef	STATESETUP
#undef	STATETEARDOWN
#undef	SETUP
#undef	onestate
#undef	INIT
#undef	INC
#undef	ISSTATEIN
#undef	FWD
#undef	BACK
#undef	ISSETBACK
#undef	SNAMES

/* macros for manipulating states, large version */
#define	states	char *
#define	CLEAR(v)	memset(v, 0, m->g->nstates)
#define	SET0(v, n)	((v)[n] = 0)
#define	SET1(v, n)	((v)[n] = 1)
#define	ISSET(v, n)	((v)[n])
#define	ASSIGN(d, s)	memcpy(d, s, m->g->nstates)
#define	EQ(a, b)	(memcmp(a, b, m->g->nstates) == 0)
#define	STATEVARS	int vn; char *space
#define	STATESETUP(m, nv)	{ (m)->space = cub_malloc(NULL, (nv)*(m)->g->nstates); \
				if ((m)->space == NULL) return(CUB_REG_ESPACE); \
				(m)->vn = 0; }
#define	STATETEARDOWN(m)	{ cub_free(NULL, (m)->space); }
#define	SETUP(v)	((v) = &m->space[m->vn++ * m->g->nstates])
#define	onestate	int
#define	INIT(o, n)	((o) = (n))
#define	INC(o)	((o)++)
#define	ISSTATEIN(v, o)	((v)[o])
/* some abbreviations; note that some of these know variable names! */
/* do "if I'm here, I can also be there" etc without branches */
#define	FWD(dst, src, n)	((dst)[here+(n)] |= (src)[here])
#define	BACK(dst, src, n)	((dst)[here-(n)] |= (src)[here])
#define	ISSETBACK(v, n)	((v)[here - (n)])
/* function names */
#define	LNAMES			/* flag */

#include "engine.c"

/*
 - cub_regexec - interface for matching
 = extern int cub_regexec(const cub_regex_t *, const char *, size_t, size_t, \
 =					cub_regmatch_t [], int);
 = #define	CUB_REG_NOTBOL	00001
 = #define	CUB_REG_NOTEOL	00002
 = #define	CUB_REG_STARTEND	00004
 = #define	CUB_REG_TRACE	00400	// tracing of execution
 = #define	CUB_REG_LARGE	01000	// force large representation
 = #define	CUB_REG_BACKR	02000	// force use of backref code
 *
 * We put this here so we can exploit knowledge of the state representation
 * when choosing which matcher to call.  Also, by this point the matchers
 * have been prototyped.
 */
int				/* 0 success, REG_NOMATCH failure */
cub_regexec(preg, string, size, nmatch, pmatch, eflags)
const cub_regex_t *preg;
const char *string;
size_t size;
size_t nmatch;
cub_regmatch_t pmatch[];
int eflags;
{
	register struct cub_re_guts *g = preg->re_g;
#ifdef REDEBUG
#	define	GOODFLAGS(f)	(f)
#else
#	define	GOODFLAGS(f)	((f)&(CUB_REG_NOTBOL|CUB_REG_NOTEOL|CUB_REG_STARTEND))
#endif
	if (!cub_malloc_ok ())
		return (CUB_REG_ESPACE);

	if (preg->re_magic != MAGIC1 || g->magic != MAGIC2)
		return(CUB_REG_BADPAT);
	assert(!(g->iflags&BAD));
	if (g->iflags&BAD)		/* backstop for no-debug case */
		return(CUB_REG_BADPAT);
	eflags = GOODFLAGS(eflags);

	if (g->nstates <= CHAR_BIT*sizeof(states1) && !(eflags&CUB_REG_LARGE))
		return(smatcher(g, (char *)string, size, nmatch, pmatch, eflags));
	else
		return(lmatcher(g, (char *)string, size, nmatch, pmatch, eflags));
}
