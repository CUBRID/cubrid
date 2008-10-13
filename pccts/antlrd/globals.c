/*
 * globals.c	--	File containing all variables/tables visible to all files.
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */
#include <stdio.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"

char Version[] = "1.00";	/* PCCTS version number */
char LexStartSymbol[] = "START";/* Name of starting lexical class/automaton */

char *DlgFileName = "parser.dlg";
char *DefFileName = "tokens.h";
char *ErrFileName = "err.c";
char *ModeFileName= "mode.h";


/* Current ambiguity examination information */
int CurAmbigAlt1, CurAmbigAlt2, CurAmbigline, CurAmbigfile;
char *CurAmbigbtype;


						/* M e t h o d  T a b l e s */
/*
 * The following tables are used to fill syntax diagram nodes with the correct
 * function pointers for computing FIRST sets and printing themselves.
 */

/* fpTraverse[node type] == pointer to function that calculates trees
 * representing the FIRST sets for that node (maintains spatial info).
 */
Tree *(*fpTraverse[NumNodeTypes+1])() = {
	NULL,
	tJunc,
	tRuleRef,
	tToken,
	tAction
};

/* fpReach[node type] == pointer to function that calculates FIRST set for
 * that node. (r stands for reach)
 */
set (*fpReach[NumNodeTypes+1])() = {
	NULL,
	rJunc,
	rRuleRef,
	rToken,
	rAction
};

/* fpPrint[node type] == pointer to function that knows how to print that node. */
void (*fpPrint[NumNodeTypes+1])() = {
	NULL,
	pJunc,
	pRuleRef,
	pToken,
	pAction
};

char *decodeJType[] = {
	"invalid",
	"aSubBlk",
	"aOptBlk",
	"aLoopBlk",
	"EndBlk",
	"RuleBlk",
	"Generic",
	"EndRule",
	"aPlusBlk",
	"aLoopBegin"
};


							/* H a s h  T a b l e s */

Entry	**Tname,			/* Table of all token names (maps name to tok num)*/
		**Texpr,			/* Table of all token expressions
							   (maps expr to tok num) */
		**Rname,			/* Table of all Rules (has ptr to start of rule) */
		**Fcache,			/* Cache of First/Follow Computations */
		**Tcache;			/* Tree cache; First/Follow for permute trees */


							/* V a r i a b l e s */

int		EpToken=0;			/* Imaginary Epsilon token number */
int		CurFile= -1;		/* Index into FileStr table */
char	*CurRule=NULL;		/* Pointer to current rule name */
RuleEntry *CurRuleNode=NULL;/* Pointer to current rule node in syntax tree */
char	*CurRetDef=NULL;	/* Pointer to current return type definition */
char	*CurParmDef=NULL;	/* Pointer to current parameter definition */
int		NumRules=0;			/* Rules are from 1 to n */
FILE	*output=NULL;		/* current parser output file */
FILE	*input=NULL;		/* current grammar input file */
char	*FileStr[MaxNumFiles];/* Ptr to array of file names on command-line */
int		NumFiles=0;			/* current grammar file number */
void	(**fpTrans)(),		/* array of ptrs to funcs that translate nodes */
	 	(**fpJTrans)();		/*  ... that translate junctions */
int		**FoStack;			/* Array of LL_k ptrs to stacks of rule numbers */
int		**FoTOS;			/* FOLLOW stack top-of-stack pointers */
Junction *SynDiag = NULL;	/* Pointer to start of syntax diagram */
int		BlkLevel=1;			/* Current block level.  Set by antlr.g, used by
							 * scanner to translate $i.j attributes */
int		TokenNum=TokenStart;
char	**TokenStr=NULL;	/* map token # to token name */
char	**ExprStr=NULL;		/* map token # to expr */
Junction **RulePtr=NULL;	/* map rule # to RuleBlk node of rule */
ListNode *ExprOrder=NULL;	/* list of exprs as they are found in grammar */
ListNode *BeforeActions=NULL;/* list of grammar actions before rules */
ListNode *AfterActions=NULL;/* list of grammar actions after rules */
ListNode *LexActions=NULL;	/* list of lexical actions */
ListNode **Cycles=NULL;		/* list of cycles (for each k) found when
							   doing FOLLOWs */
ListNode *eclasses=NULL;	/* list of error classes */
LClass	 lclass[MaxLexClasses]; /* array of lex class definitions */
int		 CurrentLexClass;	/* index into lclass */
int		 NumLexClasses=0;	/* in range 1..MaxLexClasses (init 0) */

char	*HdrAction=NULL;	/* action defined with #header */
FILE	*ErrFile ;	/* sets and error recovery stuff */
FILE	*DefFile=NULL;		/* list of tokens, return value structs, setwd defs */
int		CannotContinue=FALSE;
int		OutputLL_k = 1;		/* LL_k for parsing must be power of 2 */
int		action_file;		/* used to track start of action */
int		action_line;

						/* C m d - L i n e  O p t i o n s */

int		LL_k=1;				/* how many tokens of lookahead */
int		PrintOut = FALSE;	/* print out the grammar */
int		PrintAnnotate = FALSE;/* annotate printout with FIRST sets */
int		CodeGen=TRUE;		/* Generate output code? */
int		LexGen=TRUE;		/* Generate lexical files? (tokens.h, parser.dlg) */
int		GenAST=FALSE;		/* Generate AST's? */
int		GenANSI=FALSE;		/* Generate ANSI code where necessary */
int		GenExprSets=TRUE;	/* use sets not (LA(1)==tok) expression lists */
int		GenCR=FALSE;		/* Generate cross reference? */
int		GenLineInfo=FALSE;	/* Generate # line "file" stuff? */
int		TraceGen=FALSE;		/* Generate code to trace rule invocation */
int		elevel=1;			/* error level for ambiguity messages */
int		GenEClasseForRules=0;/* don't generate eclass for each rule */
