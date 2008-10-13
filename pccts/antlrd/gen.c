/*
 * gen.c
 *
 * Functions to generate code for the target language: C
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"

					/* T r a n s l a t i o n  T a b l e s */

/* C_Trans[node type] == pointer to function that knows how to translate that node. */
void (*C_Trans[NumNodeTypes+1])() = {
	NULL,
	NULL,					/* See next table.  Junctions have many types */
	genRuleRef,
	genToken,
	genAction
};

/* C_JTrans[Junction type] == pointer to function that knows how to translate that
 * kind of junction node.
 */
void (*C_JTrans[NumJuncTypes+1])() = {
	NULL,
	genSubBlk,
	genOptBlk,
	genLoopBlk,
	genEndBlk,
	genRule,
	genJunction,
	genEndRule,
	genPlusBlk,
	genLoopBegin
};

/* TJP--Commented this out since it conflicts with global def
 *
#define ALT	0			/* token number for alternative (junction) in expr trees */
#define PastWhiteSpace(s)	while (*(s) == ' ' || *(s) == '\t' || *(s) == '\n') {s++;}

static int tabs = 0;
#define TAB { int i; for (i=0; i<tabs; i++) putc('\t', output); }
static void tab() TAB
#ifdef __STDC__
static ActionNode *findImmedAction( Node * );
static void dumpRetValAssign(char *, char *);
static void dumpAfterActions(FILE *output);
#else
static ActionNode *findImmedAction();
static void dumpRetValAssign();
#endif


#define gen(s)		{tab(); fprintf(output, s);}
#define gen1(s,a)		{tab(); fprintf(output, s,a);}
#define gen2(s,a,b)		{tab(); fprintf(output, s,a,b);}
#define gen3(s,a,b,c)	{tab(); fprintf(output, s,a,b,c);}
#define gen4(s,a,b,c,d)	{tab(); fprintf(output, s,a,b,c,d);}
#define gen5(s,a,b,c,d,e)	{tab(); fprintf(output, s,a,b,c,d,e);}
#define gen6(s,a,b,c,d,e,f)	{tab(); fprintf(output, s,a,b,c,d,e,f);}

#define _gen(s)			{fprintf(output, s);}
#define _gen1(s,a)		{fprintf(output, s,a);}
#define _gen2(s,a,b)	{fprintf(output, s,a,b);}
#define _gen3(s,a,b,c)	{fprintf(output, s,a,b,c);}
#define _gen4(s,a,b,c,d){fprintf(output, s,a,b,c,d);}
#define _gen5(s,a,b,c,d,e){fprintf(output, s,a,b,c,d,e);}

#define genErrClause(f)	_gen1("else zzFAIL(zzerr%d);\n", DefErrSet( &f ))


void
freeBlkFsets(q)
Junction *q;
{
	int i;
	Junction *alt;
	require(q!=NULL, "genBlk: invalid node");

	for (alt=q; alt != NULL; alt= (Junction *) alt->p2 )
	{
		for (i=1; i<=LL_k; i++) set_free(alt->fset[i]);
	}
}

static void
BLOCK_Head()
{
	gen("{\n");
	tabs++;
	gen1("zzBLOCK(zztasp%d);\n", BlkLevel);
}

static void
BLOCK_Tail()
{
	gen1("zzEXIT(zztasp%d);\n", BlkLevel);
	gen("}\n");
	tabs--;
	gen("}\n");
}

static void
BLOCK_Preamble(q)
Junction *q;
{
	ActionNode *a;

	BLOCK_Head();
	if ( q->jtype == aPlusBlk ) gen("int zzcnt=1;\n");
	if ( q->parm != NULL ) gen1("zzaPush(%s);\n", q->parm)
	else gen("zzMake0;\n");
	gen("{\n");
	if ( q->jtype == aLoopBegin )
		a = findImmedAction( ((Junction *)q->p1)->p1 );	/* look at aLoopBlk */
	else
		a = findImmedAction( q->p1 );
	if ( a!=NULL ) {
		dumpAction(a->action, output, tabs, a->file, a->line);
		a->action = NULL;	/* remove action. We have already handled it */
	}
}

static void
genExprTree(t,k)
Tree *t;
int k;
{
	require(t!=NULL, "genExprTree: NULL tree");
	
	if ( t->token == ALT )
	{
		_gen("("); genExprTree(t->down, k); _gen(")");
		if ( t->right!=NULL )
		{
			_gen("||");
			_gen("("); genExprTree(t->right, k); _gen(")");
		}
		return;
	}
	if ( t->down!=NULL ) _gen("(");
	_gen1("LA(%d)==",k);
	if ( TokenStr[t->token] == NULL ) _gen1("%d", t->token)
	else _gen1("%s", TokenStr[t->token]);
	if ( t->down!=NULL )
	{
		_gen("&&");
		_gen("("); genExprTree(t->down, k+1); _gen(")");
	}
	if ( t->down!=NULL ) _gen(")");
	if ( t->right!=NULL )
	{
		_gen("||");
		_gen("("); genExprTree(t->right, k); _gen(")");
	}
}

#ifdef DUM
static void
genExprTree(t,k)
Tree *t;
int k;
{
	require(t!=NULL, "genExprTree: NULL tree");
	
	if ( t->token == ALT )
	{
		genExprTree(t->down, k);
		if ( t->right!=NULL ) {_gen("||"); genExprTree(t->right, k);}
		return;
	}
	_gen("(");
	_gen1("LA(%d)==",k);
	if ( TokenStr[t->token] == NULL ) _gen1("%d", t->token)
	else _gen1("%s", TokenStr[t->token]);
	if ( t->down!=NULL ) {_gen("&&"); genExprTree(t->down, k+1);}
	_gen(")");
	if ( t->right!=NULL ) {_gen("||"); genExprTree(t->right, k);}
}
#endif

/*
 * Generate LL(k) type expressions of the form:
 *
 *		 (LA(1) == T1 || LA(1) == T2 || ... || LA(1) == Tn) &&
 *		 (LA(2) == T1 || LA(2) == T2 || ... || LA(2) == Tn) &&
 *			.....
 *		 (LA(k) == T1 || LA(k) == T2 || ... || LA(k) == Tn)
 *
 * If GenExprSets generate:
 *
 *		(setwdi[LA(1)]&(1<<j)) && (setwdi[LA(2)]&(1<<j)) ...
 *
 * where n is set_deg(expr) and Ti is some random token and k is the last nonempty
 * set in fset.
 * k=1..LL_k where LL_k >= 1.
 * This routine is visible only to this file and cannot answer a TRANS message.
 */
static void
genExpr(j)
Junction *j;
{
	int k = 1;
	unsigned *e, *g, firstTime=1;
	set *fset = j->fset;

	if ( GenExprSets )
	{
		while ( !set_nil(fset[k]) )
		{
			if ( set_deg(fset[k])==1 )	/* too simple for a set? */
			{
				int e;
				_gen1("(LA(%d)==",k);
				e = set_int(fset[k]);
				if ( TokenStr[e] == NULL ) _gen1("%d)", e)
				else _gen1("%s)", TokenStr[e]);
			}
			else
			{
				NewSet();
				FillSet( fset[k] );
				_gen3("(zzsetwd%d[LA(%d)]&0x%x)", wordnum, k, 1<<setnum);
			}
			if ( k == LL_k ) break;
			k++;
			if ( !set_nil(fset[k]) ) _gen(" && ");
		}
		if ( j->ftree != NULL )
		{
			/*fprintf(stderr, "genExpr ftree:"); preorder(j->ftree); fprintf(stderr, "\n");
			*/
			_gen(" && !("); genExprTree(j->ftree, 1); _gen(")");
		}
		return;
	}
	while ( !set_nil(fset[k]) )
	{
		if ( (e=g=set_pdq(fset[k])) == NULL ) fatal("genExpr: cannot allocate IF expr pdq set");
		for (; *e!=nil; e++)
		{
			if ( !firstTime ) _gen(" || ") else { _gen("("); firstTime = 0; }
			_gen1("LA(%d)==",k);
			if ( TokenStr[*e] == NULL ) _gen1("%d", *e)
			else _gen1("%s", TokenStr[*e]);
		}
		free( g );
		_gen(")");
		if ( k == LL_k ) break;
		k++;
		if ( !set_nil(fset[k]) ) { firstTime=1; _gen(" && "); }
	}
	if ( j->ftree != NULL )
	{
		/*fprintf(stderr, "genExpr ftree:"); preorder(j->ftree); fprintf(stderr, "\n");*/
		_gen(" && !("); genExprTree(j->ftree, 1); _gen(")");
	}
}

/*
 * Generate code for any type of block.  If the last alternative in the block is
 * empty (not even an action) don't bother doing it.  This permits us to handle
 * optional and loop blocks as well.
 *
 * Only do this block, return after completing the block.
 * This routine is visible only to this file and cannot answer a TRANS message.
 */
static
set
genBlk(q,jtype)
Junction *q;
int jtype;
{
	set f;
	Junction *alt;
	require(q!=NULL,				"genBlk: invalid node");
	require(q->ntype == nJunction,	"genBlk: not junction");

	if ( q->p2 == NULL )	/* only one alternative?  Then don't need if */
	{	
		TRANS(q->p1);
		return empty;		/* no decision to be made-->no error set */
	}
	f = First(q, 1, jtype);
	for (alt=q; alt != NULL; alt= (Junction *) alt->p2 )
	{
		if ( alt->p2 == NULL )					/* chk for empty alt */
		{	
			Node *p = alt->p1;
			if ( p->ntype == nJunction )
			{
				/* we have empty alt */
				if ( ((Junction *)p)->p1 == (Node *)q->end )
				{
					break;						/* don't do this one, quit */
				}
			}
		}
		if ( alt != q ) gen("else ")
		else tab();
		_gen("if ( ");
		genExpr(alt);
		_gen(" ) ");
		_gen("{\n");
		tabs++;
		TRANS(alt->p1);
		--tabs;
		gen("}\n");
	}
	return f;
}

/* Generate an action.  Don't if action is NULL which means that it was already
 * handled as an init action.
 */
void
genAction(p)
ActionNode *p;
{
	require(p!=NULL,			"genAction: invalid node and/or rule");
	require(p->ntype==nAction,	"genAction: not action");
	
	if ( p->action != NULL )
		dumpAction(p->action, output, tabs, p->file, p->line);
	TRANS(p->next)
}

/*
 *		if invoking rule has !noAST pass zzSTR to rule ref and zzlink it in
 *		else pass addr of temp root ptr (&_ast) (don't zzlink it in).
 *
 *		if ! modifies rule-ref, then never link it in and never pass zzSTR.
 *		Always pass address of temp root ptr.
 */
void
genRuleRef(p)
RuleRefNode *p;
{
	Junction *q;
	RuleEntry *r, *r2;
	char *parm = "";
	require(p!=NULL,			"genRuleRef: invalid node and/or rule");
	require(p->ntype==nRuleRef, "genRuleRef: not rule reference");
	
	r = (RuleEntry *) hash_get(Rname, p->text);
	if ( r == NULL ) {warnNoFL( eMsg1("rule %s not defined", p->text) ); return;}
	r2 = (RuleEntry *) hash_get(Rname, p->rname);
	if ( r2 == NULL ) {warnNoFL("Rule hash table is screwed up beyond belief"); return;}

	tab();
	if ( GenAST )
	{
		if ( r2->noAST || p->astnode==ASTexclude )
		{
			_gen("_ast = NULL; ");
			parm = "&_ast";
		}
		else parm = "zzSTR";
		if ( p->assign!=NULL )
		{
			if ( !HasComma(p->assign) ) {_gen1("%s = ",p->assign);}
			else _gen1("{ struct _rv%d _trv; _trv = ", r->rulenum);
		}
		_gen4("%s(%s%s%s);", p->text,
							   parm,
							   (p->parms!=NULL)?",":"",
							   (p->parms!=NULL)?p->parms:"");
		if ( !r2->noAST && p->astnode == ASTinclude )
		{
			_gen(" zzlink(_root, &_sibling, &_tail);");
		}
	}
	else
	{
		if ( p->assign!=NULL )
		{
			if ( !HasComma(p->assign) ) {_gen1("%s = ",p->assign);}
			else _gen1("{ struct _rv%d _trv; _trv = ", r->rulenum);
		}
		_gen2("%s(%s);", p->text, (p->parms!=NULL)?p->parms:"");
	}
	q = RulePtr[r->rulenum];	/* find definition of ref'd rule */
	if ( p->assign!=NULL )
		if ( HasComma(p->assign) )
		{
			dumpRetValAssign(p->assign, q->ret);
			_gen("}");
		}
	_gen("\n");
	TRANS(p->next)
}

/*
 * Generate code to match a token.
 *
 * Getting the next token is tricky.  We want to ensure that any action
 * following a token is executed before the next GetToken();
 */ 
void
genToken(p)
TokNode *p;
{
	RuleEntry *r;
	ActionNode *a;
	require(p!=NULL,			"genToken: invalid node and/or rule");
	require(p->ntype==nToken,	"genToken: not token");
	
	r = (RuleEntry *) hash_get(Rname, p->rname);
	if ( r == NULL ) {warnNoFL("Rule hash table is screwed up beyond belief"); return;}
	if ( TokenStr[p->token]!=NULL ) gen1("zzmatch(%s);", TokenStr[p->token])
	else gen1("zzmatch(%d);", p->token);
	a = findImmedAction( p->next );
	if ( GenAST )
	{
		if ( !r->noAST )
		{
			if ( p->astnode==ASTchild )
				{_gen(" zzsubchild(_root, &_sibling, &_tail);");}
			else if ( p->astnode==ASTroot )
				{_gen(" zzsubroot(_root, &_sibling, &_tail);");}
			else
				{_gen(" zzastDPush;");}
		}
		else _gen(" zzastDPush;");
	}
	if ( a != NULL )
	{
		_gen("\n");
		dumpAction(a->action, output, tabs, a->file, a->line);
		gen("zzCONSUME;\n");
		TRANS( a->next );
	}
	else
	{
		_gen(" zzCONSUME;\n");
		TRANS(p->next);
	}
}

void
genOptBlk(q)
Junction *q;
{
	set f;
	require(q!=NULL,				"genOptBlk: invalid node and/or rule");
	require(q->ntype == nJunction,	"genOptBlk: not junction");
	require(q->jtype == aOptBlk,	"genOptBlk: not optional block");

	BLOCK_Preamble(q);
	BlkLevel++;
	f = genBlk(q, aOptBlk);
	set_free(f);
	freeBlkFsets(q);
	BlkLevel--;
	BLOCK_Tail();
	if (q->end->p1 != NULL) TRANS(q->end->p1);
}

/*
 * Generate code for a loop blk of form:
 *
 *				 |---|
 *				 v   |
 *			   --o-G-o-->o--
 */
void
genLoopBlk(q,begin)
Junction *q, *begin;
{
	set f;
	require(q->ntype == nJunction,	"genLoopBlk: not junction");
	require(q->jtype == aLoopBlk,	"genLoopBlk: not loop block");

	if ( q->visited ) return;
	q->visited = TRUE;
	if ( q->p2 == NULL )	/* only one alternative? */
	{
		gen("while ( ");
		f = First(q, 1, aLoopBlk);
		if ( begin!=NULL )
		{
			genExpr(begin);
		}
		else genExpr(q);
		_gen(" ) {\n");
		tabs++;
		TRANS(q->p1);
		gen1("zzLOOP(zztasp%d);\n", BlkLevel-1);
		--tabs;
		gen("}\n");
		freeBlkFsets(q);
		set_free(f);
		q->visited = FALSE;
		return;
	}
	gen("while ( 1 ) {\n");
	tabs++;
	if ( begin!=NULL )
	{
		gen("if ( ");
		genExpr((Junction *)begin->p2);
		_gen(" ) break;\n");
	}
	f = genBlk(q, aLoopBlk);
	set_free(f);
	freeBlkFsets(q);
	if ( begin==NULL ) gen("else break;\n");		/* code for exiting loop */
	gen1("zzLOOP(zztasp%d);\n", BlkLevel-1);
	--tabs;
	gen("}\n");
	q->visited = FALSE;
}

/*
 * Generate code for a loop blk of form:
 *
 * 				         |---|
 *					     v   |
 *			   --o-->o-->o-G-o-->o--
 *                   |           ^
 *                   v           |
 *					 o-----------o
 *
 * q->end points to the last node (far right) in the blk.  Note that q->end->jtype
 * must be 'EndBlk'.
 *
 * Generate code roughly of the following form:
 *
 *	do {
 *		... code for alternatives ...
 *  } while ( First Set of aLoopBlk );
 *
 *	OR if > 1 alternative
 *
 *	do {
 *		... code for alternatives ...
 *		else break;
 *  } while ( 1 );
 */
void
genLoopBegin(q)
Junction *q;
{
	set f;
	int i;
	require(q!=NULL,				"genLoopBegin: invalid node and/or rule");
	require(q->ntype == nJunction,	"genLoopBegin: not junction");
	require(q->jtype == aLoopBegin,	"genLoopBegin: not loop block");
	require(q->p2!=NULL,			"genLoopBegin: invalid Loop Graph");

	BLOCK_Preamble(q);
	BlkLevel++;
	f = First(q, 1, aLoopBegin);
	if ( LL_k>1 )
	{
		if ( !set_nil(q->fset[2]) )	genLoopBlk( (Junction *)q->p1, q );
		else genLoopBlk( (Junction *)q->p1, NULL );
	}
	else genLoopBlk( (Junction *)q->p1, NULL );
	for (i=1; i<=LL_k; i++) set_free(q->fset[i]);
	for (i=1; i<=LL_k; i++) set_free(((Junction *)q->p2)->fset[i]);
	--BlkLevel;
	BLOCK_Tail();
	set_free(f);
	if (q->end->p1 != NULL) TRANS(q->end->p1);
}

/*
 * Generate code for a loop blk of form:
 *
 * 					 |---|
 *					 v   |
 *			       --o-G-o-->o--
 *
 * q->end points to the last node (far right) in the blk.  Note that q->end->jtype
 * must be 'EndBlk'.
 *
 * Generate code roughly of the following form:
 *
 *	do {
 *		... code for alternatives ...
 *  } while ( First Set of aPlusBlk );
 *
 *	OR if > 1 alternative
 *
 *	do {
 *		... code for alternatives ...
 *		else if not 1st time through, break;
 *  } while ( 1 );
 */
void
genPlusBlk(q)
Junction *q;
{
	set f;
	require(q!=NULL,				"genPlusBlk: invalid node and/or rule");
	require(q->ntype == nJunction,	"genPlusBlk: not junction");
	require(q->jtype == aPlusBlk,	"genPlusBlk: not Plus block");

	if ( q->visited ) return;
	q->visited = TRUE;
	BLOCK_Preamble(q);
	BlkLevel++;
	if ( q->p2 == NULL )	/* only one alternative? */
	{
		gen("do {\n");
		tabs++;
		TRANS(q->p1);
		gen1("zzLOOP(zztasp%d);\n", BlkLevel-1);
		--tabs;
		gen("} while ( ");
		f = First(q, 1, aPlusBlk);
		genExpr(q);
		_gen(" );\n");
		--BlkLevel;
		BLOCK_Tail();
		q->visited = FALSE;
		freeBlkFsets(q);
		set_free(f);
		if (q->end->p1 != NULL) TRANS(q->end->p1);
		return;
	}
	gen("do {\n");
	tabs++;
	f = genBlk(q, aPlusBlk);
	gen("else if ( zzcnt>1 ) break; ");/* code for exiting loop */
	genErrClause(f);
	set_free(f);
	freeBlkFsets(q);
	gen1("zzcnt++; zzLOOP(zztasp%d);\n", BlkLevel-1);
	--tabs;
	gen("} while ( 1 );\n");
	--BlkLevel;
	BLOCK_Tail();
	q->visited = FALSE;
	if (q->end->p1 != NULL) TRANS(q->end->p1);
}

/*
 * Generate code for a sub blk of alternatives of form:
 *
 *			       --o-G1--o--
 *					 |     ^
 *					 v    /|
 *			         o-G2-o|
 *					 |     ^
 *					 v     |
 *				   ..........
 *					 |     ^
 *					 v    /
 *			         o-Gn-o
 *
 * q points to the 1st junction of blk (upper-left).
 * q->end points to the last node (far right) in the blk.  Note that q->end->jtype
 * must be 'EndBlk'.
 * The last node in every alt points to q->end.
 *
 * Generate code of the following form:
 *	if ( First(G1) ) {
 *		...code for G1...
 *	}
 *	else if ( First(G2) ) {
 *		...code for G2...
 *	}
 *	...
 *	else {
 *		...code for Gn...
 *	}
 */
void
genSubBlk(q)
Junction *q;
{
	set f;
	require(q->ntype == nJunction,	"genSubBlk: not junction");
	require(q->jtype == aSubBlk,	"genSubBlk: not subblock");

	BLOCK_Preamble(q);
	BlkLevel++;
	f = genBlk(q, aSubBlk);
	if ( q->p2 != NULL ) {tab(); genErrClause(f);}
	set_free(f);
	freeBlkFsets(q);
	--BlkLevel;
	BLOCK_Tail();
	if (q->end->p1 != NULL) TRANS(q->end->p1);
}

/*
 * Generate code for a rule.
 *
 *		rule--> o-->o-Alternatives-o-->o
 * Or,
 *		rule--> o-->o-Alternative-o-->o
 *
 * The 1st junction is a RuleBlk.  The second can be a SubBlk or just a junction
 * (one alternative--no block), the last is EndRule.
 * The second to last is EndBlk if more than one alternative exists in the rule.
 *
 * To get to the init-action for a rule, we must bypass the RuleBlk,
 * and possible SubBlk.
 * Mark any init-action as generated so genBlk() does not regenerate it.
 */
void
genRule(q)
Junction *q;
{
	set follow, rk, f;
	ActionNode *a;
	RuleEntry *r;
	static int file = -1;
	require(q->ntype == nJunction,	"genRule: not junction");
	require(q->jtype == RuleBlk,	"genRule: not rule");

restart:
	r = (RuleEntry *) hash_get(Rname, q->rname);
	if ( r == NULL ) warnNoFL("Rule hash table is screwed up beyond belief");
	if ( q->file != file )		/* open new output file if need to */
	{
		if ( output != NULL ) fclose( output );
		output = fopen(outname(FileStr[q->file]), "w");
		require(output != NULL, "genRule: can't open output file");
		if ( file == -1 ) genHdr1(q->file);
		else genHdr(q->file);
		file = q->file;
	}
	if ( q->ret!=NULL )
	{
		if ( HasComma(q->ret) ) {
		    gen1("\nstatic \tstruct _rv%d\n",r->rulenum);
		}
		else
		{
			gen("\nstatic ");
			DumpType(q->ret, output);
			gen("\n");
		}
	}
	else gen("\nstatic void ");
	gen1("%s(", q->rname);
	if ( GenAST )
	{
		if ( GenANSI ) {_gen("AST **_root");}
		else _gen("_root");
		if ( q->pdecl!=NULL ) _gen(",");
	}

	if ( GenANSI ) {_gen1("%s)\n{\n", (q->pdecl!=NULL)?q->pdecl:"");}
	else
	{
		DumpListOfParmNames( q->pdecl, output );
		gen(")\n");
		if ( GenAST ) gen("AST **_root;\n");
		DumpOldStyleParms( q->pdecl, output );
		gen("{\n");
	}
	tabs++;
	if ( q->ret!=NULL )
	{
		if ( HasComma(q->ret) ) {gen1("struct _rv%d _retv;\n",r->rulenum);}
		else
		{
			tab();
			DumpType(q->ret, output);
			gen(" _retv = 0;\n");
		}
	}
	gen("zzRULE;\n");
	gen1("zzBLOCK(zztasp%d);\n", BlkLevel);
	gen("zzMake0;\n");
	gen("{\n");
	
	/* L o o k  F o r  I n i t  A c t i o n */
	if ( ((Junction *)q->p1)->jtype == aSubBlk )
		a = findImmedAction( ((Junction *)q->p1)->p1 );
	else
		a = findImmedAction( q->p1 );	/* only one alternative in rule */
	if ( a!=NULL )
	{
		dumpAction(a->action, output, tabs, a->file, a->line);
		a->action = NULL;	/* remove action. We have already handled it */
	}
	if ( TraceGen ) gen1("zzTRACE(\"%s\");\n", q->rname);

	BlkLevel++;
	q->visited = TRUE;				/* mark RULE as visited for FIRST/FOLLOW */
	f = genBlk((Junction *)q->p1, RuleBlk);
	if ( q->p1 != NULL )
		if ( ((Junction *)q->p1)->p2 != NULL )
			{tab(); genErrClause(f);}
	set_free(f);
	freeBlkFsets((Junction *)q->p1);
	q->visited = FALSE;
	--BlkLevel;
	gen1("zzEXIT(zztasp%d);\n", BlkLevel);
	if ( q->ret!=NULL ) gen("return _retv;\n") else gen("return;\n");
	/* E r r o r  R e c o v e r y */
	NewSet();
	rk = empty;
	REACH(q->end, 1, &rk, follow);
	FillSet( follow );
	set_free( follow );
	_gen("fail:\n");
	gen("zzEXIT(zztasp1);\n");
	if ( q->erraction!=NULL )
		dumpAction(q->erraction, output, tabs, q->file, q->line);
	gen1("zzsyn(zzlextext, LA(1), %s, zzMissSet, zzMissTok);\n", r->egroup==NULL?"\"\"":r->egroup);
	gen2("zzresynch(zzsetwd%d, 0x%x);\n", wordnum, 1<<setnum);
	if ( q->ret!=NULL ) gen("return _retv;\n");
	gen("}\n");
	tabs--;
	gen("}\n");
	if ( q->p2 != NULL ) {TRANS(q->p2);} /* generate code for next rule too */
	else dumpAfterActions( output );
}

void
genJunction(q)
Junction *q;
{
	require(q->ntype == nJunction,	"genJunction: not junction");
	require(q->jtype == Generic,	"genJunction: not generic junction");

	if ( q->p1 != NULL ) TRANS(q->p1);
	if ( q->p2 != NULL ) TRANS(q->p2);
}

void
genEndBlk(q)
Junction *q;
{
}

void
genEndRule(q)
Junction *q;
{
}

void
genHdr(file)
int file;
{
	_gen("/*\n");
	_gen(" * A n t l r  T r a n s l a t i o n  H e a d e r\n");
	_gen(" *\n");
	_gen(" * Terence Parr, Hank Dietz and Will Cohen: 1989, 1990, 1991\n");
	_gen(" * Purdue University Electrical Engineering\n");
	_gen1(" * ANTLR Version %s\n", Version);
	_gen(" */\n");
	if ( GenLineInfo ) _gen2(LineInfoFormatStr, 1, FileStr[file]);
	if ( HdrAction != NULL ) dumpAction( HdrAction, output, 0, -1, 0);
	if ( LL_k > 1 ) _gen1("#define LL_K %d\n", OutputLL_k);
	if ( GenAST ) _gen("#define GENAST\n\n");
	_gen("#include \"zzpref.h\"\n");
	_gen("#include \"antlr.h\"\n");
	if ( GenAST ) _gen("#include \"ast.h\"\n\n");
	_gen1("#include \"%s\"\n", DefFileName);
	_gen("#include \"dlgdef.h\"\n");
	_gen1("#include \"%s\"\n", ModeFileName); 
}

void
genHdr1(file)
int file;
{
	ListNode *p;

	genHdr(file);
	if ( GenAST )
	{
		_gen("#include \"ast.c\"\n");
		_gen("zzASTgvars\n\n");
	}
	_gen("ANTLR_INFO\n");
	if ( BeforeActions != NULL )
	{
		for (p = BeforeActions->next; p!=NULL; p=p->next)
			dumpAction( p->elem, output, 0, -1, 0);
	}
}

/* dump action 's' to file 'output' starting at "local" tab 'tabs'
   Dump line information in front of action if GenLineInfo is set
   If file == -1 then GenLineInfo is ignored.
   The user may redefine the LineInfoFormatStr to his/her liking
   most compilers will like the default, however.
*/
void
dumpAction(s,output,tabs,file,line)
char *s;
FILE *output;
int tabs, file, line;
{
    int inDQuote, inSQuote;
    require(s!=NULL, 		"dumpAction: NULL action");
    require(output!=NULL,	eMsg1("dumpAction: output FILE is NULL for %s",s));

	if ( GenLineInfo && file != -1 )
	{
		fprintf(output, LineInfoFormatStr, line, FileStr[file]);
	}
	TAB;
    PastWhiteSpace( s );
    inDQuote = inSQuote = FALSE;
    while ( *s != '\0' )
    {
        if ( *s == '\\' )
        {
            putc( *s++, output ); /* Avoid '"' Case */
            if ( *s == '\0' ) return;
            if ( *s == '\'' ) putc( *s++, output );
            if ( *s == '\"' ) putc( *s++, output );
        }
        if ( *s == '\'' )
        {
            if ( !inDQuote ) inSQuote = !inSQuote;
        }
        if ( *s == '"' )
        {
            if ( !inSQuote ) inDQuote = !inDQuote;
        }
        if ( *s == '\n' )
        {
            putc('\n', output);
            PastWhiteSpace( s );
            if ( *s == '}' )
            {
                --tabs;
				TAB;
                putc( *s++, output );
                continue;
            }
            if ( *s == '\0' ) return;
			if ( *s != '#' )	/* #define, #endif etc.. start at col 1 */
            {
				TAB;
			}
        }
        if ( *s == '}' && !(inSQuote || inDQuote) )
        {
            --tabs;            /* Indent one fewer */
        }
        if ( *s == '{' && !(inSQuote || inDQuote) )
        {
            tabs++;            /* Indent one more */
        }
        putc( *s, output );
        s++;
    }
    putc('\n', output);
}

static
void dumpAfterActions(output)
FILE *output;
{
	ListNode *p;
	require(output!=NULL, "genRule: output file was NULL for some reason");
	if ( AfterActions != NULL )
	{
		for (p = AfterActions->next; p!=NULL; p=p->next)
			dumpAction( p->elem, output, 0 );
	}
	fclose( output );
}

/*
 * Find the next action in the stream of execution.  Do not pass
 * junctions with more than one path leaving them.
 * Only pass generic junctions.
 *
 *	Scan forward while (generic junction with p2==NULL)
 *	If we stop on an action, return ptr to the action
 *	else return NULL;
 */
static ActionNode *
findImmedAction(q)
Node *q;
{
	Junction *j;
	require(q!=NULL, "findImmedAction: NULL node");
	require(q->ntype>=1 && q->ntype<=NumNodeTypes, "findImmedAction: invalid node");
	
	while ( q->ntype == nJunction )
	{
		j = (Junction *)q;
		if ( j->jtype != Generic || j->p2 != NULL ) return NULL;
		q = j->p1;
		if ( q == NULL ) return NULL;
	}
	if ( q->ntype == nAction ) return (ActionNode *)q;
	return NULL;
}

static void
dumpRetValAssign(retval, ret_def)
char *retval, *ret_def;
{
	char *p, *q = ret_def;
	int field =1;
	
	tab();
	while ( *retval != '\0' )
	{
		while ( isspace((*retval)) ) retval++;
/*		p = retval; */
		while ( *retval!=',' && *retval!='\0' ) putc(*retval++, output);
/*		fprintf(output, " = _trv.f%d; ", field++); */
		fprintf(output, " = _trv.");
		
		DumpNextNameInDef(&q, output);
/*		while ( p!=retval ) putc(*p++, output); */
		putc(';', output); putc(' ', output);
		if ( *retval == ',' ) retval++;
	}
}
