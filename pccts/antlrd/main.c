/*
 * main.c -- main program for PCCTS ANTLR.
 *
 * Terence Parr
 * Purdue University
 * July 1991
 * $Revision: 1.3 $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MAKE_OPTION		/* doesn't work right now */
#include <sys/types.h>
#include <sys/times.h>
#include <sys/stat.h>
#endif
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "attrib.h"
#include "tokens.h"
#include "dlgdef.h"

		/* C m d - L i n e  O p t i o n  S t r u c t  &  F u n c s */

typedef struct {
			char *option;
			int  arg;
			void (*process)();
			char *descr;
		} Opt;

static void pFile(s)
char *s;
{
	if ( *s=='-' ) { warnNoFL( eMsg1("invalid option: '%s'",s) ); return; }
	require(NumFiles<MaxNumFiles,"exceeded max # of input files");
	FileStr[NumFiles++] = s;
}

void pLLK(s,t)
char *s,*t;
{
	OutputLL_k = LL_k = atoi(t);
	if ( LL_k <= 0 ) {
		warnNoFL("must have at least one token of look-ahead (setting to 1)");
		LL_k = 1;
	}
	if ( ((LL_k-1)&LL_k)!=0 ) { /* output ll(k) must be power of 2 */
		int n;
		for(n=1; n<LL_k; n<<=1) {;}
		OutputLL_k = n;
	}
}

void pCGen()	{ CodeGen = FALSE; LexGen = FALSE; }
void pLGen()	{ LexGen = FALSE; }
void pTGen()	{ TraceGen = TRUE; }
void pSGen()	{ GenExprSets = FALSE; }
void pPrt()		{ PrintOut = TRUE; pCGen(); pLGen(); }
void pPrtA()	{ PrintOut = TRUE; PrintAnnotate = TRUE; pCGen(); pLGen(); }
void pAst()		{ GenAST = TRUE; }
void pANSI()	{ GenANSI = TRUE; }
void pCr()		{ GenCR = TRUE; }
void pLI()		{ GenLineInfo = TRUE; }
void pFe(s,t) char *s, *t; {ErrFileName = t;}
void pFl(s,t) char *s, *t; {DlgFileName = t;}
void pFt(s,t) char *s, *t; {DefFileName = t;}
void pFm(s,t) char *s, *t; {ModeFileName = t;}
void pE1()		{ elevel = 1; }
void pE2()		{ elevel = 2; }
void pE3()		{ elevel = 3; }
void pEGen()	{ GenEClasseForRules = 1; }

Opt options[] = {
    { "-cr", 0, pCr,	"Generate cross reference (default=FALSE)"},
    { "-e1", 0, pE1,	"Ambiguities/errors shown in low detail (default)"},
    { "-e2", 0, pE2,	"Ambiguities/errors shown in more detail"},
    { "-e3", 0, pE3,	"Ambiguities/errors shown in excrutiating detail"},
    { "-fe", 1, pFe,	"Rename err.c"},
    { "-fl", 1, pFl,	"Rename lexical output--parser.dlg"},
    { "-ft", 1, pFt,	"Rename tokens.h"},
    { "-fm", 1, pFm,	"Rename mode.h"},
    { "-ga", 0, pANSI,	"Generate ANSI-compatible code (default=FALSE)"},
    { "-gt", 0, pAst,	"Generate code for Abstract-Syntax-Trees (default=FALSE)"},
    { "-gc", 0, pCGen,	"Do not generate output parser code (default=FALSE)"},
    { "-ge", 0, pEGen,	"Generate an error class for each non-terminal (default=FALSE)"},
    { "-gs", 0, pSGen,	"Do not generate sets for token expression lists (default=FALSE)"},
    { "-gd", 0, pTGen,	"Generate code to trace rule invocation (default=FALSE)"},
	{ "-gl", 0, pLI,	"Generate line info about grammar actions in C parser"},
    { "-gx", 0, pLGen,	"Do not generate lexical (dlg-related) files (default=FALSE)"},
    { "-k",  1, pLLK,	"Set k of LL(k) -- tokens of look-ahead (default==1)"},
    { "-p",  0, pPrt,	"Print out the grammar w/o actions (default=no)"},
    { "-pa", 0, pPrtA,	"Print out the grammar w/o actions & w/FIRST sets (default=no)"},
	{ "*",   0, pFile, 	"" },	/* anything else is a file */
	{ NULL,  0, NULL }
};

void pEset(e) char *e; { require(e!=NULL, "NULL element"); fprintf(stderr, " %s", e); }

void readDescr();
void cleanUp();
static void help(), init(), buildRulePtr();

								/* M a i n */

int main(argc,argv)
int argc;
char *argv[];
{

	fprintf(stderr, "Antlr parser generator   Version %s   1989, 1990, 1991\n", Version);
	if ( argc == 1 ) { help(); return 1; }
	ProcessArgs(argc-1, &(argv[1]), options);

	fpTrans = &(C_Trans[0]);		/* Translate to C Language */
	fpJTrans = &(C_JTrans[0]);
	init();
	lexclass(LexStartSymbol);

	readDescr();
	if ( CannotContinue ) {cleanUp(); return 1;}
	if ( HdrAction == NULL ) warnNoFL("no #header action was found");

	EpToken = addTname("[Ep]");		/* add imaginary token epsilon */
	set_size(NumWords(TokenNum-1));
	
	if ( CodeGen ) genDefFile();	/* create tokens.h */
	if ( LexGen ) genLexDescr();	/* create parser.dlg */

	buildRulePtr();					/* create mapping from rule # to RuleBlk junction */
	ComputeErrorSets();
	FoLink( (Node *)SynDiag );		/* add follow links to end of all rules */
	if ( GenCR ) GenCrossRef( SynDiag );

	if ( CodeGen )
	{
		if ( SynDiag == NULL )
		{
			warnNoFL("no grammar description recognized");
			cleanUp();
			return 1;
		}
		else
		{
			ErrFile = fopen(ErrFileName, "w");
			require(ErrFile != NULL, "main: can't open err file");
			NewSetWd();
			GenErrHdr();
			TRANS(SynDiag);			/* Translate to the target language */
			DumpSetWd();
			fclose( ErrFile );
		}
	}

	if ( PrintOut )
	{
		if ( SynDiag == NULL ) {warnNoFL("no grammar description recognized");}
		else PRINT(SynDiag);
	}

	cleanUp();
	return 0;						/* report all is well for make etc... */
}

static void
init()
{
	Tname = newHashTable();
/*	Texpr = newHashTable();*/
	Rname = newHashTable();
	Fcache = newHashTable();
	Tcache = newHashTable();
	TokenStr = (char **) calloc(TSChunk, sizeof(char *));
	require(TokenStr!=NULL, "main: cannot allocate TokenStr");
/*
	ExprStr = (char **) calloc(TSChunk, sizeof(char *));
	require(ExprStr!=NULL, "main: cannot allocate ExprStr");
*/
	FoStack = (int **) calloc(LL_k+1, sizeof(int *));
	require(FoStack!=NULL, "main: cannot allocate FoStack");
	FoTOS = (int **) calloc(LL_k+1, sizeof(int *));
	require(FoTOS!=NULL, "main: cannot allocate FoTOS");
	Cycles = (ListNode **) calloc(LL_k+1, sizeof(ListNode *));
	require(Cycles!=NULL, "main: cannot allocate Cycles List");
}

static
void
help()
{
	Opt *p = options;
	fprintf(stderr, "antlr [options] f1 f2 ... fn\n");
	while ( *(p->option) != '*' )
	{
		fprintf(stderr, "\t%s %s\t%s\n",
						p->option,
						(p->arg)?"___":"   ",
						p->descr);
		p++;
	}
}

/* The RulePtr array is filled in here.  RulePtr exists primarily
 * so that sets of rules can be maintained for the FOLLOW caching
 * mechanism found in rJunc().  RulePtr maps a rule num from 1 to n
 * to a pointer to its RuleBlk junction where n is the number of rules.
 */
static void
buildRulePtr()
{
	int r=1;
	Junction *p  = SynDiag;
	RulePtr = (Junction **) calloc(NumRules+1, sizeof(Junction *));
	require(RulePtr!=NULL, "cannot allocate RulePtr array");
	
	while ( p!=NULL )
	{
		require(r<=NumRules, "too many rules???");
		/*fprintf(stderr, "RulePtr[%d] points to %s\n",r, p->rname);*/
		RulePtr[r++] = p;
		p = (Junction *)p->p2;
	}
}

void
readDescr()
{
	input = NextFile();
	require(input!=NULL, "No grammar description found (exiting...)");
	ANTLR(grammar, input);
}

FILE *
NextFile()
{
	FILE *f;

	for (;;)
	{
		CurFile++;
		if ( CurFile >= NumFiles ) return(NULL);
		f = fopen(FileStr[CurFile], "r");
		if ( f == NULL )
		{
			warnNoFL( eMsg1("file %s doesn't exist; ignored", FileStr[CurFile]) );
		}
		else
		{
			aSourceFile = FileStr[CurFile];
			return(f);
		}
	}
}

/*
 * Return a string corresponding to the output file name associated
 * with the input file name passed in.
 *
 * Observe the following rules:
 *
 *		f.e		--> f".c"
 *		f		--> f".c"
 *		f.		--> f".c"
 *		f.e.g	--> f.e".c"
 *
 * Where f,e,g are arbitrarily long sequences of characters in a file
 * name.
 *
 * In other words, if a ".x" appears on the end of a file name, make it
 * ".c".  If no ".x" appears, append ".c" to the end of the file name.
 *
 * Use malloc() for new string.
 */
char *
outname(fs)
char *fs;
{
	static char buf[MaxFileName+1];
	char *p;

	p = buf;
	strcpy(buf, fs);
	while ( *p != '\0' )  {p++;}			/* Stop on '\0' */
	while ( *p != '.' && p != buf ) {--p;}	/* Find '.' */
	if ( p != buf ) *p = '\0';				/* Found '.' */
	require(strlen(buf) + 2 < MaxFileName, "outname: filename too big");
	strcat(buf, ".c");
	/* yaw */
	for ( --p; p != buf && *p != '/'; --p ) ;
	if ( p != buf && *p == '/' )
		return ++p;
	return( buf );
}

void
fatalFL(err,f,l)
char *err, *f;
int l;
{
	fprintf(stderr, ErrHdr, f, l);
	fprintf(stderr, " %s\n", err);
	cleanUp();
	exit(-1);
}

void
cleanUp()
{
	if ( DefFile != NULL) fclose( DefFile );
}

/* sprintf up to 3 strings */
char *
eMsg3(s,a1,a2,a3)
char *s, *a1, *a2, *a3;
{
	static char buf[250];			/* DANGEROUS as hell !!!!!! */
	
	sprintf(buf, s, a1, a2, a3);
	return( buf );
}

/* sprintf a decimal */
char *
eMsgd(s,d)
char *s;
int d;
{
	static char buf[250];			/* DANGEROUS as hell !!!!!! */
	
	sprintf(buf, s, d);
	return( buf );
}

void s_fprT(f, e)
FILE *f;
set e;
{
	register unsigned *p;
	unsigned *q;

	if ( set_nil(e) ) return;
	if ( (q=p=set_pdq(e)) == NULL ) fatal("Can't alloc space for set_pdq");
	fprintf(f, "{");
	while ( *p != nil )
	{
		fprintf(f, " %s", (TokenStr[*p]!=NULL) ? TokenStr[*p]:
		                   (ExprStr[*p]!=NULL) ? ExprStr[*p] : "");
		p++;
	}
	fprintf(f, " }");
	free(q);
}

void ProcessArgs(argc, argv, options)
int argc;
char **argv;
Opt *options;
{
	Opt *p;
	require(argv!=NULL, "ProcessArgs: command line NULL");

	while ( argc-- > 0 )
	{
		p = options;
		while ( p->option != NULL )
		{
			if ( strcmp(p->option, "*") == 0 ||
				 strcmp(p->option, *argv) == 0 )
			{
				if ( p->arg )
				{
					(*p->process)( *argv, *(argv+1) );
					argv++;
					argc--;
				}
				else
					(*p->process)( *argv );
				break;
			}
			p++;
		}
		argv++;
	}
}

#ifdef MAKE_OPTION /* doesn't work right now */
/*
 * call stat() on the filename and outname(filename)
 * If the grammar file is new than the output filename or the
 * output filename does not exist, return 1 else return 0.
 */
int
MakeParserFile(filename)
char *filename;
{
	struct stat gfile, cfile;

	if ( stat(filename, &gfile)==-1 ) return 1;
	if ( stat(outname(filename), &cfile)==-1 ) return 1;
	/* both files exist, so we now check last modification times */
	if ( gfile.st_mtime > cfile.st_mtime ) return 1;
	return 0;
}
#endif
