/*
 * bits.c -- manage creation and output of bit sets used by the parser.
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"

#define BitsPerByte		8
#define BitsPerWord		BitsPerByte*sizeof(unsigned long)

static unsigned long *setwd = NULL;
int setnum = -1;
int wordnum = 0;

int esetnum = 0;

/* Create a new setwd (ignoring [Ep] token on end) */
void
NewSetWd()
{
	unsigned long *p;

	if ( setwd == NULL )
	{
		setwd = (unsigned long *) calloc(TokenNum, sizeof(unsigned long));
		require(setwd!=NULL, "NewSetWd: cannot alloc set wd\n");
	}
	for (p = setwd; p<&(setwd[TokenNum]); p++)  {*p=0;}
	wordnum++;
}

/* Dump the current setwd to ErrFile. 0..MaxTokenVal */
void
DumpSetWd()
{
	int i,c=1;

	if ( setwd==NULL ) return;
	fprintf(DefFile, "#define zzsetwd%d ZZ_PREF(zzsetwd%d)\n", 
		wordnum, wordnum );
	fprintf(DefFile, "extern unsigned long zzsetwd%d[];\n", wordnum);

	fprintf(ErrFile, "unsigned long ZZ_PREF(zzsetwd%d)[%d] = {", 
		wordnum, TokenNum-1);
	for (i=0; i<TokenNum-1; i++)
	{
		if ( i!=0 ) fprintf(ErrFile, ",");
		if ( c == 8 ) {fprintf(ErrFile, "\n\t"); c=1;} else c++;
		fprintf(ErrFile, "0x%x", setwd[i]);
	}
	fprintf(ErrFile, "};\n");
}

/* Make a new set.  Dump old setwd and create new setwd if current setwd is full */
void
NewSet()
{
	setnum++;
	if ( setnum==BitsPerWord )		/* is current setwd full? */
	{
		DumpSetWd(); NewSetWd(); setnum = 0;
	}
}

/* s is a set of tokens.  Turn on bit at each token position in set 'setnum' */
void
FillSet(s)
set s;
{
	unsigned long mask=(1<<setnum);
	int e;

	while ( !set_nil(s) )
	{
		e = set_int(s);
		set_rm(e, s);
		setwd[e] |= mask;
	}
}

					/* E r r o r  C l a s s  S t u f f */

/* compute the FIRST of a rule for the error class stuff */
static set
Efirst(rule, eclass)
char *rule;
ECnode *eclass;
{
	set rk, a;
	Junction *r;
	RuleEntry *q = (RuleEntry *) hash_get(Rname, rule);

	if ( q == NULL )
	{
		warnNoFL(eMsg2("undefined rule '%s' referenced in errclass '%s'; ignored",
						rule, TokenStr[eclass->tok]));
		return empty;
	}
	r = RulePtr[q->rulenum];
	r->end->halt = TRUE;		/* don't let reach fall off end of rule here */
	rk = empty;
	REACH(r, 1, &rk, a);
	r->end->halt = FALSE;
	return a;
}

/* scan the list of tokens/eclasses/nonterminals filling the new eclass with the
 * set described by the list.  Note that an eclass can be quoted to allow spaces
 * etc... However, an eclass must not conflict with a reg expr found elsewhere.
 * The reg expr will be taken over the eclass name.
 */
static void
doEclass(eclass)
char *eclass;		/* pointer to Error Class */
{
	TermEntry *q;
	ECnode *p;
	ListNode *e;
	int t, deg=0;
	set a;
	require(eclass!=NULL, "doEclass: NULL eset");
	
	p = (ECnode *) eclass;
	lexmode(p->lexclass);	/* switch to lexclass where errclass is defined */
	p->eset = empty;
	for (e = (p->elist)->next; e!=NULL; e=e->next)
	{
		if ( islower( *(e->elem) ) )	/* is it a rule ref? (alias FIRST request) */
		{
			a = Efirst(e->elem, p);
			set_orin(&p->eset, a);
			deg += set_deg(a);
			set_free( a );
			continue;
		}
		else if ( *(e->elem)=='"' )
		{
			t = 0;
			q = (TermEntry *) hash_get(Texpr, e->elem);
			if ( q == NULL )
			{
				/* if quoted and not an expr look for eclass name */
				q = (TermEntry *) hash_get(Tname, e->elem=StripQuotes(e->elem));
				if ( q != NULL ) t = q->token;
			}
			else t = q->token;
		}
		else	/* labelled token/eclass */
		{
			q = (TermEntry *) hash_get(Tname, e->elem);
			if ( q != NULL ) t = q->token; else t=0;
		}
		if ( t!=0 )
		{
			set_orel(t, &p->eset);
			deg++;
		}
		else warnNoFL(eMsg2("undefined token '%s' referenced in errclass '%s'; ignored",
							e->elem, TokenStr[p->tok]));
	}
	p->setdeg = deg;
}

void
ComputeErrorSets()
{
	list_apply(eclasses, doEclass);
}

/* replace a subset of an error set with an error class name if a subset is found
 * repeat process until no replacements made
 */
void
SubstErrorClass(f)
set *f;
{
	int max, done = 0;
	ListNode *p;
	ECnode *ec, *maxclass = NULL;
	set a;
	require(f!=NULL, "SubstErrorClass: NULL eset");

	if ( eclasses == NULL ) return;
	while ( !done )
	{
		max = 0;
		maxclass = NULL;
		for (p=eclasses->next; p!=NULL; p=p->next)	/* chk all error classes */
		{
			ec = (ECnode *) p->elem;
			if ( ec->setdeg > max )
			{
				if ( set_sub(ec->eset, *f) || set_equ(ec->eset, *f) )
					{maxclass = ec; max=ec->setdeg;}
			}
		}
		if ( maxclass != NULL )	/* if subset found, replace with token */
		{
			a = set_dif(*f, maxclass->eset);
			set_orel(maxclass->tok, &a);
			set_free(*f);
			*f = a;
		}
		else done = 1;
	}
}

/* Define a new error set.  WARNING...set-implementation dependent */
int
DefErrSet(f)
set *f;
{
	unsigned long *p, *endp;
	int e=1;
	require(!set_nil(*f), "DefErrSet: nil set to dump?");

	SubstErrorClass(f);
	p = (unsigned long *) f->setword;
	endp = (unsigned long *) &(f->setword[NumWords(TokenNum-1)]);
	esetnum++;
	fprintf(DefFile, "#define zzerr%d ZZ_PREF(zzerr%d)\n", 
		esetnum, esetnum);
	fprintf(DefFile, "extern unsigned long zzerr%d[];\n", esetnum);
	fprintf(ErrFile, "unsigned long ZZ_PREF(zzerr%d)[%d] = {", 
		esetnum, NumWords(TokenNum-1));
	while ( p < endp )
	{
		if ( e > 1 ) fprintf(ErrFile, ", ");
		fprintf(ErrFile, "0x%x", *p++);
		if ( e == 7 )
		{
			if ( p < endp ) fprintf(ErrFile, ",");
			fprintf(ErrFile, "\n\t");
			e=1;
		}
		else e++;
	}
	fprintf(ErrFile, "};\n");

	return esetnum;
}

void
GenErrHdr()
{
	int i, j;

	fprintf(ErrFile, "/*\n");
	fprintf(ErrFile, " * A n t l r  S e t s / E r r o r  F i l e  H e a d e r\n");
	fprintf(ErrFile, " *\n");
	fprintf(ErrFile, " * Generated from:");
	for (i=0; i<NumFiles; i++) fprintf(ErrFile, " %s", FileStr[i]);
	fprintf(ErrFile, "\n");
	fprintf(ErrFile, " *\n");
	fprintf(ErrFile, " * Terence Parr, Hank Dietz and Will Cohen: 1989, 1990, 1991\n");
	fprintf(ErrFile, " * Purdue University Electrical Engineering\n");
	fprintf(ErrFile, " * ANTLR Version %s\n", Version);
	fprintf(ErrFile, " */\n\n");
	if ( HdrAction != NULL ) dumpAction( HdrAction, ErrFile, 0, -1, 0 );
	if ( LL_k > 1 ) fprintf(ErrFile, "#define LL_K %d\n", OutputLL_k);
	fprintf(ErrFile, "#define zzEOF_TOKEN %d\n", EofToken);
	fprintf(ErrFile, "#define zzSET_SIZE %d\n", NumWords(TokenNum-1));
	fprintf(ErrFile, "#include \"zzpref.h\"\n");
	fprintf(ErrFile, "#include \"antlr.h\"\n");
	fprintf(ErrFile, "#include \"dlgdef.h\"\n");
	fprintf(ErrFile, "#include \"err.h\"\n\n");

	/* Dump a zztokens for each automaton */
	fprintf(ErrFile, "const char *zztokens[%d]={\n", TokenNum-1);
	fprintf(ErrFile, "\t/* 00 */\t\"Invalid\",\n");
	if ( TokenStr[EofToken] != NULL )
		fprintf(ErrFile, "\t/* %02d */\t\"%s\"", EofToken, TokenStr[i]);
	else
		fprintf(ErrFile, "\t/* %02d */\t\"EOF\"", EofToken);
	for (i=TokenStart; i<TokenNum-1; i++)
	{
		if ( i == EpToken ) continue;
		if ( TokenStr[i] != NULL )
			fprintf(ErrFile, ",\n\t/* %02d */\t\"%s\"", i, TokenStr[i]);
		else
		{
			/* look in all lexclasses for the reg expr */
			for (j=0; j<NumLexClasses; j++)
			{
				lexmode(j);
				if ( ExprStr[i] != NULL )
				{
					fprintf(ErrFile, ",\n\t/* %02d */\t", i);
					dumpExpr(ExprStr[i]);
					break;
				}
			}
			require(j<NumLexClasses, eMsg1("No label or expr for token %d",i));
		}
	}
	fprintf(ErrFile, "\n};\n");
}

void
dumpExpr(e)
char *e;
{
	while ( *e!='\0' )
	{
		if ( *e=='\\' && *(e+1)=='\\' )
			{putc('\\', ErrFile); putc('\\', ErrFile); e+=2;}
		else if ( *e=='\\' && *(e+1)=='"' )
			{putc('\\', ErrFile); putc('"', ErrFile); e+=2;}
		else if ( *e=='\\' ) {putc('\\', ErrFile); putc('\\', ErrFile); e++;}
		else {putc(*e, ErrFile); e++;}
	}
}

