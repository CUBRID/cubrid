/*
 * lex.c	--	Generate all of the lexical type files: parser.dlg tokens.h
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"

#define DLGErrorString "invalid token"

/* Generate a complete lexical description of the lexemes found in the grammar */
void
genLexDescr()
{
	TermEntry *t;
	ListNode *p;
	FILE *dlgFile = fopen(DlgFileName, "w");
	require(dlgFile!=NULL, eMsg1("genLexFile: cannot open %s", DlgFileName) );

	fprintf(dlgFile, "<<\n");
	fprintf(dlgFile, "/* %s -- DLG Description of scanner\n", DlgFileName);
	fprintf(dlgFile, " *\n");
	fprintf(dlgFile, " * Generated from:");
	{int i; for (i=0; i<NumFiles; i++) fprintf(dlgFile, " %s", FileStr[i]);}
	fprintf(dlgFile, "\n");
	fprintf(dlgFile, " *\n");
	fprintf(dlgFile, " * Terence Parr, Hank Dietz and Will Cohen: 1989, 1990, 1991\n");
	fprintf(dlgFile, " * Purdue University Electrical Engineering\n");
	fprintf(dlgFile, " * ANTLR Version %s\n", Version);
	fprintf(dlgFile, " */\n");

	if ( HdrAction != NULL ) dumpAction( HdrAction, dlgFile, 0, -1, 0 );
	if ( LL_k > 1 ) fprintf(dlgFile, "#define LL_K %d\n", OutputLL_k);
	fprintf(dlgFile, "#include \"zzpref.h\"\n");
	fprintf(dlgFile, "#include \"antlr.h\"\n");
	if ( GenAST ) fprintf(dlgFile, "#include \"ast.h\"\n");
       	fprintf(dlgFile, "#define INSIDE_SCAN_DOT_C 1\n");
	fprintf(dlgFile, "#include \"%s\"\n", DefFileName);
	fprintf(dlgFile, "#include \"dlgdef.h\"\n");
	fprintf(dlgFile, "LOOKAHEAD\n");
	fprintf(dlgFile, "void zzerraction()\n");
	fprintf(dlgFile, "{\n");
	fprintf(dlgFile, "\t(*zzerr)(\"%s\");\n", DLGErrorString);
	fprintf(dlgFile, "\tzzadvance();\n");
	fprintf(dlgFile, "\tzzskip();\n");
	fprintf(dlgFile, "}\n>>\n\n");
	/* dump all actions */
	for (p = LexActions->next; p!=NULL; p=p->next)
	{
		fprintf(dlgFile, "<<\n");
		dumpAction( p->elem, dlgFile, 0, -1, 0 );
		fprintf(dlgFile, ">>\n");
	}
	/* dump all regular expression rules/actions (skip sentinel node) */
	if ( ExprOrder == NULL ) {
		warnNoFL("no regular expressions found in grammar");
	}
	else dumpLexClasses(dlgFile);
	fprintf(dlgFile, "%%%%\n");
	fclose( dlgFile );
}

/* For each lexical class, scan ExprOrder looking for expressions
 * in that lexical class.  Print out only those that match.
 * Each element of the ExprOrder list has both an expr and an lclass
 * field.
 */
void dumpLexClasses(dlgFile)
FILE *dlgFile;
{
	int i;
	TermEntry *t;
	ListNode *p;
	Expr *q;

	for (i=0; i<NumLexClasses; i++)
	{
		fprintf(dlgFile, "\n%%%%%s\n\n", lclass[i].class);
		for (p=ExprOrder->next; p!=NULL; p=p->next)
		{
			q = (Expr *) p->elem;
			if ( q->lclass != i ) continue;
			lexmode(i);
			t = (TermEntry *) hash_get(Texpr, q->expr);
			require(t!=NULL, eMsg1("genLexDescr: rexpr %s not in hash table",q->expr) );
			if ( t->token == EpToken ) continue;
			fprintf(dlgFile, "%s\n\t<<\n", StripQuotes(q->expr));
			if ( TokenStr[t->token] != NULL )
				fprintf(dlgFile, "\t\tLA(1) = %s;\n", TokenStr[t->token]);
			else fprintf(dlgFile, "\t\tLA(1) = %d;\n", t->token);
			if ( t->action != NULL ) dumpAction( t->action, dlgFile, 2,-1,0 );
			fprintf(dlgFile, "\t>>\n\n");
		}
	}
}

/* Generate a list of #defines && list of struct definitions for
 * aggregate retv's */
void
genDefFile()
{
	Junction *p  = SynDiag;
	int i;

	DefFile = fopen(DefFileName, "w");
	require(DefFile!=NULL, eMsg1("genDefFile: cannot open %s", DefFileName) );
	fprintf(DefFile, "/* %s -- List of labelled tokens\n", DefFileName);
	fprintf(DefFile, " *\n");
	fprintf(DefFile, " * Generated from:");
	for (i=0; i<NumFiles; i++) fprintf(DefFile, " %s", FileStr[i]);
	fprintf(DefFile, "\n");
	fprintf(DefFile, " *\n");
	fprintf(DefFile, " * Terence Parr, Hank Dietz and Will Cohen: 1989, 1990, 1991\n");
	fprintf(DefFile, " * Purdue University Electrical Engineering\n");
	fprintf(DefFile, " * ANTLR Version %s\n", Version);
	fprintf(DefFile, " */\n");
	if ( TokenStr[EofToken]!=NULL )
		fprintf(DefFile, "#define %s %d\n", TokenStr[EofToken], EofToken);
	for (i=TokenStart; i<TokenNum; i++)
	{
		if ( TokenStr[i]!=NULL && i != EpToken )
		{
			TermEntry *p;

			require((p=(TermEntry *)hash_get(Tname, TokenStr[i])) != NULL,
					"token not in sym tab when it should be");
			if ( !p->errclassname )
			{
				fprintf(DefFile, "#define %s %d\n", TokenStr[i], i);
			}
		}
	}

	/* Find all return types/parameters that require structs and def
	 * all rules with ret types
	 */
	i = 1;
	fprintf(DefFile, "#ifndef INSIDE_SCAN_DOT_C\n");
	while ( p!=NULL )
	{
#if 0
	    fprintf(DefFile, "#define %s ZZ_PREF(%s)\n", p->rname, p->rname);
#endif
		if ( p->ret != NULL )
		{
			if ( HasComma(p->ret) )
			{
				DumpRetValStruct(DefFile, p->ret, i);
				fprintf(DefFile, "static struct _rv%d %s(", i, p->rname);
			}
			else
			{
			        fprintf(DefFile, "static ");
				DumpType(p->ret, DefFile);
				fprintf(DefFile, " %s(", p->rname);
			}
		}
		else
		{
			fprintf(DefFile, "static void %s(", p->rname);
		}
		if ( (p->pdecl != NULL || GenAST) && GenANSI )
		{
			if ( GenAST ) fprintf(DefFile, "AST **%s",(p->pdecl!=NULL)?",":"");
			if ( p->pdecl!=NULL ) fprintf(DefFile, "%s", p->pdecl);
		}
		fprintf(DefFile, ");\n\n");
		i++;
		p = (Junction *)p->p2;
	}
	fprintf(DefFile, "#endif\n");
}

/* Given a list of ANSI-style parameter declarations, print out a
 * comma-separated list of the symbols (w/o types).
 * Basically, we look for a comma, then work backwards until start of
 * the symbol name.  Then print it out until 1st non-alnum char.  Now,
 * move on to next parameter.
 */
void
DumpListOfParmNames( pdecl, output )
char *pdecl;
FILE *output;
{
	char *p, *end;
	int firstTime = 1, done = 0;
	require(output!=NULL, "DumpListOfParmNames: NULL parm");

	if ( pdecl == NULL ) return;
	p = pdecl;
	while ( !done )
	{
		if ( !firstTime ) putc(',', output);
		done = DumpNextNameInDef(&pdecl, output);
		firstTime = 0;
#ifdef DUM
		while ( *p!='\0' && *p!=',' ) p++;		/* find end of decl */
		if ( *p == '\0' ) done = 1;
		while ( !isalnum(*p) && *p!='_' ) --p;	/* scan back until valid var character */
		while ( isalnum(*p) || *p=='_' ) --p;	/* scan back until beginning of variable */
		p++;						/* move to start of variable */
		if ( !firstTime ) putc(',', output);
		while ( isalnum(*p) || *p=='_'  ) {putc(*p, output); p++;}
		while ( *p!='\0' && *p!=',' ) p++;		/* find end of decl */
		p++;						/* move past this parameter */
		firstTime = 0;
#endif
	}
}

/* given a list of parameters or return values, dump the next
 * name to output.  Return 1 if last one just printed, 0 if more to go.
 */
int DumpNextNameInDef(q, output)
char **q;				/* pointer to ptr into definition string */
FILE *output;
{
	char *p = *q;		/* where did we leave off? */
	int done=0;

	while ( *p!='\0' && *p!=',' ) p++;		/* find end of decl */
	if ( *p == '\0' ) done = 1;
	while ( !isalnum(*p) && *p!='_' ) --p;	/* scan back until valid var character */
	while ( isalnum(*p) || *p=='_' ) --p;	/* scan back until beginning of variable */
	p++;						/* move to start of variable */
	while ( isalnum(*p) || *p=='_'  ) {putc(*p, output); p++;}
	while ( *p!='\0' && *p!=',' ) p++;		/* find end of decl */
	p++;				/* move past this parameter */

	*q = p;				/* record where we left off */
	return done;
}

/* Given a list of ANSI-style parameter declarations, dump K&R-style
 * declarations, one per line for each parameter.  Basically, convert
 * comma to semi-colon, newline.
 */
void
DumpOldStyleParms( pdecl, output )
char *pdecl;
FILE *output;
{
	require(output!=NULL, "DumpOldStyleParms: NULL parm");

	if ( pdecl == NULL ) return;
	while ( *pdecl != '\0' )
	{
		if ( *pdecl == ',' )
		{
			pdecl++;
			putc(';', output); putc('\n', output);
			while ( *pdecl==' ' || *pdecl=='\t' || *pdecl=='\n' ) pdecl++;
		}
		else {putc(*pdecl, output); pdecl++;}
	}
	putc(';', output);
	putc('\n', output);
}

/* Take in a type definition (type + symbol) and print out type only */
void
DumpType(s, f)
char *s;
FILE *f;
{
	char *p, *end;
	require(s!=NULL, "DumpType: invalid type string");

	p = &s[strlen(s)-1];		/* start at end of string and work back */
	/* scan back until valid variable character */
	while ( !isalnum(*p) && *p!='_' ) --p;
	/* scan back until beginning of variable */
	while ( isalnum(*p) || *p=='_' ) --p;
	if ( p<=s )
	{
		warnNoFL(eMsg1("invalid parameter/return value: '%s'",s));
		return;
	}
	end = p;					/* here is where we stop printing alnum */
	p = s;
	while ( p!=end ) {putc(*p, f); p++;} /* dump until just before variable */
	while ( *p!='\0' )					 /* dump rest w/o variable */
	{
		if ( !isalnum(*p) || *p=='_' ) putc(*p, f);
		p++;
	}
}

/* check to see if string e is a member of string s */
int
strmember(s, e)
char *s, *e;
{
	register char *p;
	require(s!=NULL&&e!=NULL, "strmember: NULL string");

	if ( *e=='\0' ) return 1;	/* empty string is always member */
	while ( *s!='\0' )
	{
		p = e;
		while ( *p!='\0' && *p==*s ) {p++; s++;}
		if ( *p=='\0' ) return 1;
		s++;
	}
	return 0;
}

int
HasComma(s)
char *s;
{
	while (*s!='\0')
		if ( *s++ == ',' ) return 1;
	return 0;
}

/* found bug 10-28-91 (field names were f1, f2, ... in gen.c). We want
 * them to be named like the ret values in the rule def. I need
 * a quick fix; I hope I fixed it over in gen.c. --TJP
 */
void
DumpRetValStruct(f,ret,i)
FILE *f;
char *ret;
int i;
{
	fprintf(f, "\nstruct _rv%d {\n", i);
	while ( *ret != '\0' )
	{
		 while ( *ret==' ' || *ret=='\t' ) ret++; /* ignore white */
		 putc('\t', f);
		 while ( *ret!=',' && *ret!='\0' ) putc(*ret++, f);
		 if ( *ret == ',' ) {putc(';', f); putc('\n', f); ret++;}
	}
	fprintf(f, ";\n};\n");
}

/* given "s" yield s else given s return s) */
char *
StripQuotes(s)
char *s;
{
    char *p;

	if ( *s == '"' )
	{
	    p = malloc(strlen(s));	/* avoid writing over orginal! */
	    strcpy(p, s+1);
	    p[ strlen(p)-1 ] = '\0';    /* remove last quote */
	    return p;
	}
	return( s );
}

