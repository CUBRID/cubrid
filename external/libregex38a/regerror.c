#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "include/regex38a.h"
#include "utils.h"
#include "regerror.ih"

/*
 = #define	CUB_REG_OKAY	 0
 = #define	CUB_REG_NOMATCH	 1
 = #define	CUB_REG_BADPAT	 2
 = #define	CUB_REG_ECOLLATE	 3
 = #define	CUB_REG_ECTYPE	 4
 = #define	CUB_REG_EESCAPE	 5
 = #define	CUB_REG_ESUBREG	 6
 = #define	CUB_REG_EBRACK	 7
 = #define	CUB_REG_EPAREN	 8
 = #define	CUB_REG_EBRACE	 9
 = #define	CUB_REG_BADBR	10
 = #define	CUB_REG_ERANGE	11
 = #define	CUB_REG_ESPACE	12
 = #define	CUB_REG_BADRPT	13
 = #define	CUB_REG_EMPTY	14
 = #define	CUB_REG_ASSERT	15
 = #define	CUB_REG_INVARG	16
 = #define	CUB_REG_ATOI	255	// convert name to number (!)
 = #define	CUB_REG_ITOA	0400	// convert number to name (!)
 */
static struct rerr {
	int code;
	char *name;
	char *explain;
} rerrs[] = {
	CUB_REG_OKAY,	"REG_OKAY",	"no errors detected",
	CUB_REG_NOMATCH,	"REG_NOMATCH",	"cub_regexec() failed to match",
	CUB_REG_BADPAT,	"REG_BADPAT",	"invalid regular expression",
	CUB_REG_ECOLLATE,	"REG_ECOLLATE",	"invalid collating element",
	CUB_REG_ECTYPE,	"REG_ECTYPE",	"invalid character class",
	CUB_REG_EESCAPE,	"REG_EESCAPE",	"trailing backslash (\\)",
	CUB_REG_ESUBREG,	"REG_ESUBREG",	"invalid backreference number",
	CUB_REG_EBRACK,	"REG_EBRACK",	"brackets ([ ]) not balanced",
	CUB_REG_EPAREN,	"REG_EPAREN",	"parentheses not balanced",
	CUB_REG_EBRACE,	"REG_EBRACE",	"braces not balanced",
	CUB_REG_BADBR,	"REG_BADBR",	"invalid repetition count(s)",
	CUB_REG_ERANGE,	"REG_ERANGE",	"invalid character range",
	CUB_REG_ESPACE,	"REG_ESPACE",	"out of memory",
	CUB_REG_BADRPT,	"REG_BADRPT",	"repetition-operator operand invalid",
	CUB_REG_EMPTY,	"REG_EMPTY",	"empty (sub)expression",
	CUB_REG_ASSERT,	"REG_ASSERT",	"\"can't happen\" -- you found a bug",
	CUB_REG_INVARG,	"REG_INVARG",	"invalid argument to regex routine",
	-1,		"",		"*** unknown regexp error code ***",
};

/*
 - cub_regerror - the interface to error numbers
 = extern size_t cub_regerror(int, const cub_regex_t *, char *, size_t);
 */
/* ARGSUSED */
#if defined _WIN32 || defined _WIN64
size_t
cub_regerror(int errcode, const cub_regex_t *preg, char *errbuf, size_t errbuf_size)
#else
size_t
cub_regerror(errcode, preg, errbuf, errbuf_size)
int errcode;
const cub_regex_t *preg;
char *errbuf;
size_t errbuf_size;
#endif
{
	register struct rerr *r;
	register size_t len;
	register int target = errcode &~ CUB_REG_ITOA;
	register char *s;
	char convbuf[50];

	if (errcode == CUB_REG_ATOI)
		s = regatoi(preg, convbuf);
	else {
		for (r = rerrs; r->code >= 0; r++)
			if (r->code == target)
				break;
	
		if (errcode&CUB_REG_ITOA) {
			if (r->code >= 0)
				(void) strcpy(convbuf, r->name);
			else
				sprintf(convbuf, "REG_0x%x", target);
			assert(strlen(convbuf) < sizeof(convbuf));
			s = convbuf;
		} else
			s = r->explain;
	}

	len = strlen(s) + 1;
	if (errbuf_size > 0) {
		if (errbuf_size > len)
			(void) strcpy(errbuf, s);
		else {
			(void) strncpy(errbuf, s, errbuf_size-1);
			errbuf[errbuf_size-1] = '\0';
		}
	}

	return(len);
}

/*
 - regatoi - internal routine to implement REG_ATOI
 == static char *regatoi(const cub_regex_t *preg, char *localbuf);
 */
static char *
regatoi(preg, localbuf)
const cub_regex_t *preg;
char *localbuf;
{
	register struct rerr *r;

	for (r = rerrs; r->code >= 0; r++)
		if (strcmp(r->name, preg->re_endp) == 0)
			break;
	if (r->code < 0)
		return("0");

	sprintf(localbuf, "%d", r->code);
	return(localbuf);
}
