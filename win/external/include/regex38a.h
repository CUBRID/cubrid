#ifndef _REGEX_H_
#define	_REGEX_H_	/* never again */
/* ========= begin header generated by ./mkh ========= */
#ifdef __cplusplus
extern "C" {
#endif

/* === regex2.h === */
typedef off_t cub_regoff_t;
typedef struct {
	int re_magic;
	size_t re_nsub;		/* number of parenthesized subexpressions */
	const char *re_endp;	/* end pointer for CUB_REG_PEND */
	struct cub_re_guts *re_g;	/* none of your business :-) */
} cub_regex_t;
typedef struct {
	cub_regoff_t rm_so;		/* start of match */
	cub_regoff_t rm_eo;		/* end of match */
} cub_regmatch_t;


/* === regcomp.c === */
extern int cub_regcomp(cub_regex_t *, const char *, int);
#define	CUB_REG_BASIC	0000
#define	CUB_REG_EXTENDED	0001
#define	CUB_REG_ICASE	0002
#define	CUB_REG_NOSUB	0004
#define	CUB_REG_NEWLINE	0010
#define	CUB_REG_NOSPEC	0020
#define	CUB_REG_PEND	0040
#define	CUB_REG_DUMP	0200


/* === regerror.c === */
#define	CUB_REG_OKAY	 	0
#define	CUB_REG_NOMATCH	 	1
#define	CUB_REG_BADPAT	 	2
#define	CUB_REG_ECOLLATE	3
#define	CUB_REG_ECTYPE	 	4
#define	CUB_REG_EESCAPE	 	5
#define	CUB_REG_ESUBREG	 	6
#define	CUB_REG_EBRACK	 	7
#define	CUB_REG_EPAREN	 	8
#define	CUB_REG_EBRACE	 	9
#define	CUB_REG_BADBR		10
#define	CUB_REG_ERANGE		11
#define	CUB_REG_ESPACE		12
#define	CUB_REG_BADRPT		13
#define	CUB_REG_EMPTY		14
#define	CUB_REG_ASSERT		15
#define	CUB_REG_INVARG		16
#define	CUB_REG_ATOI		255	/* convert name to number (!) */
#define	CUB_REG_ITOA		0400	/* convert number to name (!) */
extern size_t cub_regerror(int, const cub_regex_t *, char *, size_t);


/* === regexec.c === */
extern int cub_regexec(const cub_regex_t *, const char *, size_t, size_t, cub_regmatch_t [], int);
#define	CUB_REG_NOTBOL	00001
#define	CUB_REG_NOTEOL	00002
#define	CUB_REG_STARTEND	00004
#define	CUB_REG_TRACE	00400	/* tracing of execution */
#define	CUB_REG_LARGE	01000	/* force large representation */
#define	CUB_REG_BACKR	02000	/* force use of backref code */


/* === regfree.c === */
extern void cub_regfree(cub_regex_t *);


/* === regmem.c === */
typedef void * (*CUB_REG_MALLOC)(void *, size_t);
typedef void * (*CUB_REG_REALLOC)(void *, void *, size_t);
typedef void (*CUB_REG_FREE)(void *, void *);
extern void cub_regset_malloc(CUB_REG_MALLOC);
extern void cub_regset_realloc(CUB_REG_REALLOC);
extern void cub_regset_free(CUB_REG_FREE);

#ifdef __cplusplus
}
#endif
/* ========= end header generated by ./mkh ========= */
#endif
