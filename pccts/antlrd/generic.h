/*
 * generic.h -- generic include stuff for new PCCTS ANTLR.
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */

#ifndef _GENERIC_H_
#define _GENERIC_H_

#define StrSame			0

/* User may redefine how line information looks */
#define LineInfoFormatStr "# %d \"%s\"\n"

/* Tree/FIRST/FOLLOW defines -- valid only after all grammar has been read */
#define ALT			TokenNum+1
#define SET			TokenNum+2
#define TREE_REF	TokenNum+3

					/* E r r o r  M a c r o s */

#ifdef MPW		/* Macintosh Programmer's Workshop */
#define ErrHdr "File \"%s\"; Line %d #"
#else
#define ErrHdr "%s, line %d:"
#endif

#define fatal(err)		fatalFL(err, __FILE__, __LINE__)
#define warnNoFL(err)	fprintf(stderr, "warning: %s\n", err);
#define warnFL(err,f,l)															\
			{fprintf(stderr, ErrHdr, f, l);										\
			fprintf(stderr, " warning: %s\n", err);}
#define warn(err)																\
			{fprintf(stderr, ErrHdr, FileStr[CurFile], lex_line);				\
			fprintf(stderr, " warning: %s\n", err);}
#define warnNoCR( err )															\
			{fprintf(stderr, ErrHdr, FileStr[CurFile], lex_line);				\
			fprintf(stderr, " warning: %s", err);}
#define eMsg1(s,a)	eMsg3(s,a,NULL,NULL)
#define eMsg2(s,a,b)	eMsg3(s,a,b,NULL)

				/* S a n i t y  C h e c k i n g */

#ifndef require
#define require(expr, err) {if ( !(expr) ) fatal(err);}
#endif

					/* L i s t  N o d e s */

typedef struct _l {
			char *elem;			/* pointer to any kind of element */
			struct _l *next;
		} ListNode;

/* Define a Cycle node which is used to track lists of cycles for later
 * reconciliation by ResolveFoCycles().
 */
typedef struct _c {
			int croot;			/* cycle root */
			set cyclicDep;		/* cyclic dependents */
			unsigned deg;		/* degree of FOLLOW set of croot */
		} Cycle;

typedef struct _e {
			int tok;			/* error class name == TokenStr[tok] */
			ListNode *elist;	/* linked list of elements in error set */
			set eset;
			int setdeg;			/* how big is the set */
			int lexclass;		/* which lex class is it in? */
		} ECnode;

#define newListNode	(ListNode *) calloc(1, sizeof(ListNode));
#define newCycle	(Cycle *) calloc(1, sizeof(Cycle));
#define newECnode	(ECnode *) calloc(1, sizeof(ECnode));


				/* H a s h  T a b l e  E n t r i e s */

typedef struct _t {				/* Token name or expression */
			char *str;
			struct _t *next;
			int token;			/* token number */
			int errclassname;	/* is it a errclass name or token */
			char *action;
		} TermEntry;

typedef struct _r {				/* Rule name and ptr to start of rule */
			char *str;
			struct _t *next;
/*			int passedSomething;/* is this rule passed something? */
/*			int returnsSomething;/* does this rule return something? */
			int rulenum;		/* RulePtr[rulenum]== ptr to RuleBlk junction */
			int noAST;			/* gen AST construction code? (def==gen code) */
			char *egroup;		/* which error group (err reporting stuff) */
		} RuleEntry;

typedef struct _f {				/* cache Fi/Fo set */
			char *str;			/* key == (rulename, computation, k) */
			struct _f *next;
			set fset;			/* First/Follow of rule */
			set rk;				/* set of k's remaining to be done after ruleref */
			int incomplete;		/* only w/FOLLOW sets.  Use only if complete */
		} CacheEntry;

#define newTermEntry(s)		(TermEntry *) newEntry(s, sizeof(TermEntry))
#define newRuleEntry(s)		(RuleEntry *) newEntry(s, sizeof(RuleEntry))
#define newCacheEntry(s)	(CacheEntry *) newEntry(s, sizeof(CacheEntry))

					/* L e x i c a l  C l a s s */

/* to switch lex classes, switch ExprStr and Texpr (hash table) */
typedef struct _lc {
			char *class, **exprs;
			Entry **htable;
		} LClass;

typedef struct _exprOrder {
			char *expr;
			int lclass;
		} Expr;


typedef Graph Attrib;

						/* M a x i m u m s */

#ifndef HashTableSize
#define HashTableSize	1353
/* was 253 */
#endif
#ifndef StrTableSize
#define StrTableSize	20000	/* all tokens, nonterminals, rexprs stored here */
/* was 15000 */
#endif
#define MaxLexClasses	50		/* how many automatons */
#define TokenStart		2		/* MUST be in 1 + EofToken */
#define EofToken		1		/* Always predefined to be 1 */
#define MaxNumFiles		20
#define MaxFileName		300		/* largest file name size */
#define MaxRuleName		100		/* largest rule name size */
#define TSChunk			100		/* how much to expand TokenStr/ExprStr each time */
#define FoStackSize		100		/* deepest FOLLOW recursion possible */

/* AST token types */
#define ASTexclude		0
#define ASTchild		1
#define ASTroot			2
#define ASTinclude		3		/* include subtree made by rule ref */

/*
#ifdef __STDC__
extern Graph buildAction(char *);
extern Graph buildToken(char *);
extern Graph buildRuleRef(char *);
extern Graph Or(Graph, Graph);
extern Graph Cat(Graph, Graph);
extern Graph makeOpt(Graph);
extern Graph makeLoop(Graph);
extern Graph makePlus(Graph);
extern Graph makeBlk(Graph);

extern void genAction(ActionNode *);
extern void genLoopBlk(Junction *, Junction *);
extern void genPlusBlk(Junction *);
extern void genLoopBegin(Junction *);
extern void genSubBlk(Junction *);
extern void genOptBlk(Junction *);
extern void genHdr(void);
extern void genHdr1(void);
extern void genToken(TokNode *);
extern void genRuleRef(RuleRefNode *);
extern void genRule(Junction *);
extern void genJunction(Junction *);
extern void genEndBlk(Junction *);
extern void genEndRule(Junction *);

extern Graph emptyAlt(void);
extern char *strdup(char *);
extern int grammar();

extern Tree *tJunc(Junction *, int, set *);
extern Tree *tRuleRef(RuleRefNode *, int, set *);
extern Tree *tToken(TokNode *, int, set *);
extern Tree *tAction(ActionNode *, int, set *);

extern set rJunc(Junction *, int, set *);
extern set rRuleRef(RuleRefNode *, int, set *);
extern set rToken(TokNode *, int, set *);
extern set rAction(ActionNode *, int, set *);

extern void pJunc(Junction *);
extern void pRuleRef(RuleRefNode *);
extern void pToken(TokNode *);
extern void pAction(ActionNode *);

extern int addTname(char *);
extern int addTexpr(char *);

extern char *eMsg3(char *,char *,char *, char *);
extern char *eMsgd(char *,int);
extern TokNode *newTokNode(void);
extern RuleRefNode *newRNode(void);
extern Junction *newJunction(void);
extern ActionNode *newActionNode(void);
extern int hasAction(char *);
extern void setHasAction(char *, char *);
extern FILE *NextFile(void);
extern FILE *input, *output;
extern Entry *newEntry(char *, int);
extern void FoLink(Node *);
extern char **TokenStr, **ExprStr;
extern void fatalFL(char *, char *, int);
extern void dumpAction(char *, FILE *, int);
extern char *outname( char * );
extern char *StripQuotes(char *s);
extern void addParm(Node *, char *);
extern void list_add(ListNode **, char *);
extern set First(Junction *, int, int);
extern char *makelocks(void);
extern void GenErrHdr();
extern void AddToSet(int);
extern void NewSet(void);
extern void DumpSetWd(void);
extern void NewSetWd(void);
extern void FillSet(set);
extern void DumpSetWd(void);
extern char *Fkey(char *, int, int);
extern void FoPush(char *, int);
extern void RegisterCycle(char *, int);
extern void ResolveFoCycles(int);
extern void genASTRoutines(void);
extern void DumpRetValStruct(FILE *, char *, int);
extern int HasComma(char *);
extern void GenCrossRef(Junction *);
extern Tree *VerifyAmbig(Junction *, Junction *, unsigned **, set *, Tree **, Tree **, int *);
extern void Tfree(Tree *);
extern Tree *trm_perm(Tree *, Tree *);
extern Tree *tleft_factor(Tree *);
extern Tree *tappend(Tree *, Tree *);
extern int DefErrSet(set *);
extern void list_apply(ListNode *, void (*)());
extern void dumpExpr(char *);
extern void Tlink(char *, char *);
extern void FoPop(int);
extern void genDefFile();
extern void genLexDescr();
extern void ComputeErrorSets();
extern void freeBlkFsets(Junction *);
extern void lexmode(int);
extern void DumpType(char *, FILE *);
extern void DumpListOfParmNames(char *, FILE *);
extern void DumpOldStyleParms(char *, FILE *);
extern int LexClassIndex(char *);
extern int DumpNextNameInDef(char **q, FILE *output);

#else

extern Graph buildAction();
extern Graph buildToken();
extern Graph buildRuleRef();
extern Graph Or();
extern Graph Cat();
extern Graph makeOpt();
extern Graph makeLoop();
extern Graph makePlus();
extern Graph makeBlk();

extern void genAction();
extern void genLoopBlk();
extern void genPlusBlk();
extern void genLoopBegin();
extern void genSubBlk();
extern void genOptBlk();
extern void genHdr();
extern void genHdr1();
extern void genToken();
extern void genRuleRef();
extern void genRule();
extern void genJunction();
extern void genEndBlk();
extern void genEndRule();

extern Graph emptyAlt();
extern char *strdup();
extern int grammar();

extern Tree *tJunc();
extern Tree *tRuleRef();
extern Tree *tToken();
extern Tree *tAction();

extern set rJunc();
extern set rRuleRef();
extern set rToken();
extern set rAction();

extern void pJunc();
extern void pRuleRef();
extern void pToken();
extern void pAction();

extern int addTname();
extern int addTexpr();

extern char *eMsg3();
extern char *eMsgd();
extern TokNode *newTokNode();
extern RuleRefNode *newRNode();
extern Junction *newJunction();
extern ActionNode *newActionNode();
extern int hasAction();
extern void setHasAction();
extern FILE *NextFile();
extern Entry *newEntry();
extern void FoLink();
extern void fatalFL();
extern void dumpAction();
extern char *outname();
extern char *StripQuotes();
extern void addParm();
extern void list_add();
extern set First();
extern char *makelocks();
extern void GenErrHdr();
extern void AddToSet();
extern void NewSet();
extern void DumpSetWd();
extern void NewSetWd();
extern void FillSet();
extern void DumpSetWd();
extern char *Fkey();
extern void FoPush();
extern void RegisterCycle();
extern void ResolveFoCycles();
extern void genASTRoutines();
extern void DumpRetValStruct();
extern int HasComma();
extern void GenCrossRef();
extern Tree *VerifyAmbig();
extern void Tfree();
extern Tree *trm_perm();
extern Tree *tleft_factor();
extern Tree *tappend();
extern int DefErrSet();
extern void list_apply();
extern void dumpExpr();
extern void Tlink();
extern void FoPop();
extern void genDefFile();
extern void genLexDescr();
extern void ComputeErrorSets();
extern void freeBlkFsets();
extern void lexmode();
extern void DumpType();
extern void DumpListOfParmNames();
extern void DumpOldStyleParms();
extern int LexClassIndex();
#endif
*/
                           /* V a r i a b l e s */

extern int curRule;
extern int tp;
extern Junction *SynDiag;
extern char Version[];
extern void (*fpPrint[])();
extern set (*fpReach[])();
extern Tree *(*fpTraverse[])();
extern void (**fpTrans)();
extern void (**fpJTrans)();
extern void (*C_Trans[])();
extern void (*C_JTrans[])();
extern int BlkLevel;
extern int CurFile;
extern char *CurRule;
extern RuleEntry *CurRuleNode;
extern char *FileStr[];
extern int NumFiles;
extern int EpToken;
extern Entry	**Tname,
				**Texpr,
				**Rname,
				**Fcache,
				**Tcache;
extern ListNode *ExprOrder;
extern ListNode **Cycles;
extern int TokenNum;
extern ListNode *BeforeActions, *AfterActions, *LexActions;
extern ListNode *eclasses;
extern char	*HdrAction;
extern FILE	*ErrFile;
extern char *ErrFileName;
extern char *DlgFileName;
extern char *DefFileName;
extern char *ModeFileName;
extern int NumRules;
extern Junction **RulePtr;
extern int LL_k;
extern char *decodeJType[];
extern int PrintOut;
extern int PrintAnnotate;
extern int CodeGen;
extern int LexGen;
extern int setnum;
extern int wordnum;
extern int GenAST;
extern int GenANSI;
extern int **FoStack;
extern int **FoTOS;
extern int GenExprSets;
extern FILE *DefFile;
extern int CannotContinue;
extern int GenCR;
extern int GenLineInfo;
extern int action_file, action_line;
extern int TraceGen;
extern int CurAmbigAlt1, CurAmbigAlt2, CurAmbigline, CurAmbigfile;
extern char *CurAmbigbtype;
extern int elevel;
extern int GenEClasseForRules;
extern FILE *input, *output;
extern char **TokenStr, **ExprStr;
extern int CurrentLexClass, NumLexClasses;
extern LClass lclass[];
extern char LexStartSymbol[];
extern char	*CurRetDef;
extern char	*CurParmDef;
extern int OutputLL_k;

#ifdef MEMCHK
#include "trax.h"
#else
#ifdef __STDC__
void *malloc(unsigned int), *calloc(unsigned int, unsigned int);
void *realloc(void *, unsigned int);
#else
char *malloc(), *calloc(), *realloc();
#endif
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#include "proto.h"
#endif
