/*
 * build.c -- functions associated with building syntax diagrams.
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */

#include <stdio.h>
#include <string.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"

#define SetBlk(g, t) {												\
			((Junction *)g.left)->jtype = t;						\
			((Junction *)g.left)->end = (Junction *) g.right;		\
			((Junction *)g.right)->jtype = EndBlk;}

/* Add the parameter string 'parm' to the parms field of a block-type junction
 * g.left points to the sentinel node on a block.  i.e. g.left->p1 points to
 * the actual junction with its jtype == some block-type.
 */
void
addParm(p,parm)
Node *p;
char *parm;
{
	char *q = malloc( strlen(parm) + 1 );
	require(p!=NULL, "addParm: NULL object\n");
	require(q!=NULL, "addParm: unable to alloc parameter\n");

	strcpy(q, parm);
	if ( p->ntype == nRuleRef )
	{
		((RuleRefNode *)p)->parms = q;
	}
	else if ( p->ntype == nJunction )
	{
		((Junction *)p)->parm = q;	/* only one parameter allowed on subrules */
	}
	else fatal("addParm: invalid node for adding parm");
}

/*
 * Build an action node for the syntax diagram
 *
 * buildAction(ACTION) ::= --o-->ACTION-->o--
 *
 * Where o is a junction node.
 */
Graph
buildAction(action, file, line)
char *action;
int file, line;
{
	Junction *j1, *j2;
	Graph g;
	ActionNode *a;
	require(action!=NULL, "buildAction: invalid action");
	
	j1 = newJunction();
	j2 = newJunction();
	a = newActionNode();
	a->action = malloc( strlen(action)+1 );
	require(a->action!=NULL, "buildAction: cannot alloc space for action\n");
	strcpy(a->action, action);
	j1->p1 = (Node *) a;
	a->next = (Node *) j2;
	g.left = (Node *) j1; g.right = (Node *) j2;
	a->file = file;
	a->line = line;
	return g;
}

/*
 * Build a token node for the syntax diagram
 *
 * buildToken(TOKEN) ::= --o-->TOKEN-->o--
 *
 * Where o is a junction node.
 */
Graph
buildToken(text)
char *text;
{
	Junction *j1, *j2;
	Graph g;
	TokNode *t;
	require(text!=NULL, "buildToken: invalid token name");
	
	j1 = newJunction();
	j2 = newJunction();
	t = newTokNode();
	if ( *text == '"' ) {t->label=FALSE; t->token = addTexpr( text );}
	else {t->label=TRUE; t->token = addTname( text );}
	j1->p1 = (Node *) t;
	t->next = (Node *) j2;
	g.left = (Node *) j1; g.right = (Node *) j2;
	return g;
}

/*
 * Build a rule reference node of the syntax diagram
 *
 * buildRuleRef(RULE) ::= --o-->RULE-->o--
 *
 * Where o is a junction node.
 *
 * If rule 'text' has been defined already, don't alloc new space to store string.
 * Set r->text to point to old copy in string table.
 */
Graph
buildRuleRef(text)
char *text;
{
	Junction *j1, *j2;
	Graph g;
	RuleRefNode *r;
	RuleEntry *p;
	require(text!=NULL, "buildRuleRef: invalid rule name");
	
	j1 = newJunction();
	j2 = newJunction();
	r = newRNode();
	r->assign = NULL;
	if ( (p=(RuleEntry *)hash_get(Rname, text)) != NULL ) r->text = p->str;
	else r->text = strdup( text );
	j1->p1  = (Node *) r;
	r->next = (Node *) j2;
	g.left = (Node *) j1; g.right = (Node *) j2;
	return g;
}

/*
 * Or two subgraphs into one graph via:
 *
 * Or(G1, G2) ::= --o-G1-o--
 *                  |    ^
 *					v    |
 *                  o-G2-o
 *
 * Set the altnum of junction starting G2 to 1 + altnum of junction starting G1.
 * If, however, the G1 altnum is 0, make it 1 and then
 * make G2 altnum = G1 altnum + 1.
 */
Graph
Or(g1,g2)
Graph g1, g2;
{
	Graph g;
	require(g1.left != NULL, "Or: invalid graph");
	require(g2.left != NULL && g2.right != NULL, "Or: invalid graph");

	((Junction *)g1.left)->p2 = g2.left;
	((Junction *)g2.right)->p1 = g1.right;
	/* set altnums */
	if ( ((Junction *)g1.left)->altnum == 0 ) ((Junction *)g1.left)->altnum = 1;
	((Junction *)g2.left)->altnum = ((Junction *)g1.left)->altnum + 1;
	g.left = g2.left;
	g.right = g1.right;
	return g;
}

/*
 * Catenate two subgraphs
 *
 * Cat(G1, G2) ::= --o-G1-o-->o-G2-o--
 * Cat(NULL,G2)::= --o-G2-o--
 * Cat(G1,NULL)::= --o-G1-o--
 */
Graph
Cat(g1,g2)
Graph g1, g2;
{
	Graph g;
	
	if ( g1.left == NULL && g1.right == NULL ) return g2;
	if ( g2.left == NULL && g2.right == NULL ) return g1;
	((Junction *)g1.right)->p1 = g2.left;
	g.left = g1.left;
	g.right = g2.right;
	return g;
}

/*
 * Make a subgraph an optional block
 *
 * makeOpt(G) ::= --o-->o-G-o-->o--
 *                      | 	    ^
 *						v  	    |
 *					    o-------o
 *
 * Note that this constructs {A|B|...|Z} as if (A|B|...|Z|) was found.
 *
 * The node on the far right is added so that every block owns its own
 * EndBlk node.
 */
Graph
makeOpt(g1)
Graph g1;
{
	Junction *j1,*j2,*p;
	Graph g;
	require(g1.left != NULL && g1.right != NULL, "makeOpt: invalid graph");

	j1 = newJunction();
	j2 = newJunction();
	((Junction *)g1.right)->p1 = (Node *) j2;	/* add node to G at end */
	g = emptyAlt();
	if ( ((Junction *)g1.left)->altnum == 0 ) ((Junction *)g1.left)->altnum = 1;
	((Junction *)g.left)->altnum = ((Junction *)g1.left)->altnum + 1;
	for(p=(Junction *)g1.left; p->p2!=NULL; p=(Junction *)p->p2)
		{;}										/* find last alt */
	p->p2 = g.left;								/* add optional alternative */
	((Junction *)g.right)->p1 = (Node *)j2;		/* opt alt points to EndBlk */
	g1.right = (Node *)j2;
	SetBlk(g1, aOptBlk);
	j1->p1 = g1.left;							/* add generic node in front */
	g.left = (Node *) j1;
	g.right = g1.right;

	return g;
}

/*
 * Make a graph into subblock
 *
 * makeBlk(G) ::= --o-->o-G-o-->o--
 *
 * The node on the far right is added so that every block owns its own
 * EndBlk node.
 */
Graph
makeBlk(g1)
Graph g1;
{
	Junction *j,*j2;
	Graph g;
	require(g1.left != NULL && g1.right != NULL, "makeBlk: invalid graph");

	j = newJunction();
	j2 = newJunction();
	((Junction *)g1.right)->p1 = (Node *) j2;	/* add node to G at end */
	g1.right = (Node *)j2;
	SetBlk(g1, aSubBlk);
	j->p1 = g1.left;							/* add node in front */
	g.left = (Node *) j;
	g.right = g1.right;

	return g;
}

/*
 * Make a subgraph into a loop (closure) block -- (...)*
 *
 * makeLoop(G) ::=       |---|
 *					     v   |
 *			   --o-->o-->o-G-o-->o--
 *                   |           ^
 *                   v           |
 *					 o-----------o
 *
 * After making loop, always place generic node out front.  It becomes
 * the start of enclosing block.  The aLoopBlk is the target of the loop.
 *
 * Loop blks have TWO EndBlk nodes--the far right and the node that loops back
 * to the aLoopBlk node.  Node with which we can branch past loop == aLoopBegin and
 * one which is loop target == aLoopBlk.
 * The branch-past (initial) aLoopBegin node has end
 * pointing to the last EndBlk node.  The loop-target node has end==NULL.
 *
 * Loop blocks have a set of locks (from 1..LL_k) on the aLoopBlk node.
 */
Graph
makeLoop(g1)
Graph g1;
{
	Junction *back, *front, *begin;
	Graph g;
	require(g1.left != NULL && g1.right != NULL, "makeLoop: invalid graph");

	back = newJunction();
	front = newJunction();
	begin = newJunction();
	g = emptyAlt();
	((Junction *)g1.right)->p2 = g1.left;		/* add loop branch to G */
	((Junction *)g1.right)->p1 = (Node *) back;	/* add node to G at end */
	((Junction *)g1.right)->jtype = EndBlk;		/* mark 1st EndBlk node */
	((Junction *)g1.left)->jtype = aLoopBlk;	/* mark 2nd aLoopBlk node */
	((Junction *)g1.left)->end = (Junction *) g1.right;
	((Junction *)g1.left)->lock = makelocks();
	g1.right = (Node *) back;
	begin->p1 = (Node *) g1.left;
	g1.left = (Node *) begin;
	begin->p2 = (Node *) g.left;				/* make bypass arc */
	((Junction *)g.right)->p1 = (Node *) back;
	SetBlk(g1, aLoopBegin);
	front->p1 = g1.left;						/* add node to front */
	g1.left = (Node *) front;

	return g1;
}

/*
 * Make a subgraph into a plus block -- (...)+ -- 1 or more times
 *
 * makeLoop(G) ::=	 |---|
 *					 v   |
 *			   --o-->o-G-o-->o--
 *
 * After making loop, always place generic node out front.  It becomes
 * the start of enclosing block.  The aPlusBlk is the target of the loop.
 *
 * Plus blks have TWO EndBlk nodes--the far right and the node that loops back
 * to the aPlusBlk node.
 *
 * Plus blocks have a set of locks (from 1..LL_k) on the aPlusBlk node.
 */
Graph
makePlus(g1)
Graph g1;
{
	Junction *j2, *j3;
	require(g1.left != NULL && g1.right != NULL, "makePlus: invalid graph");

	j2 = newJunction();
	j3 = newJunction();
	if ( ((Junction *)g1.left)->altnum == 0 ) ((Junction *)g1.left)->altnum = 1;
	((Junction *)g1.right)->p2 = g1.left;		/* add loop branch to G */
	((Junction *)g1.right)->p1 = (Node *) j2;	/* add node to G at end */
	((Junction *)g1.right)->jtype = EndBlk;		/* mark 1st EndBlk node */
	g1.right = (Node *) j2;
	SetBlk(g1, aPlusBlk);
	((Junction *)g1.left)->lock = makelocks();
	j3->p1 = g1.left;							/* add node to front */
	g1.left = (Node *) j3;
	
	return g1;
}

/*
 * Return an optional path:  --o-->o--
 */
Graph
emptyAlt()
{
	Junction *j1, *j2;
	Graph g;

	j1 = newJunction();
	j2 = newJunction();
	j1->p1 = (Node *) j2;
	g.left = (Node *) j1;
	g.right = (Node *) j2;
	
	return g;
}

/* N o d e  A l l o c a t i o n */

TokNode *
newTokNode()
{
	static TokNode *FreeList = NULL;
	TokNode *p, *newblk;

	if ( FreeList == NULL )
	{
		newblk = (TokNode *)calloc(TokenBlockAllocSize, sizeof(TokNode));
		if ( newblk == NULL )
			fatal(eMsg1("out of memory while building rule '%s'",CurRule));
		for (p=newblk; p<&(newblk[TokenBlockAllocSize]); p++)
		{
			p->next = (Node *)FreeList;	/* add all new token nodes to FreeList */
			FreeList = p;
		}
	}
	p = FreeList;
	FreeList = (TokNode *)FreeList->next;/* remove a Junction node */
	p->next = NULL;						/* NULL the ptr we used */

	p->ntype = nToken;
	p->rname = CurRule;
	p->file = CurFile;
	p->line = lex_line;
	
	return p;
}

RuleRefNode *
newRNode()
{
	static RuleRefNode *FreeList = NULL;
	RuleRefNode *p, *newblk;

	if ( FreeList == NULL )
	{
		newblk = (RuleRefNode *)calloc(RRefBlockAllocSize, sizeof(RuleRefNode));
		if ( newblk == NULL )
			fatal(eMsg1("out of memory while building rule '%s'",CurRule));
		for (p=newblk; p<&(newblk[RRefBlockAllocSize]); p++)
		{
			p->next = (Node *)FreeList;	/* add all new rref nodes to FreeList */
			FreeList = p;
		}
	}
	p = FreeList;
	FreeList = (RuleRefNode *)FreeList->next;/* remove a Junction node */
	p->next = NULL;						/* NULL the ptr we used */

	p->ntype = nRuleRef;
	p->rname = CurRule;
	p->file = CurFile;
	p->line = lex_line;
	p->astnode = ASTinclude;
	
	return p;
}

Junction *
newJunction()
{
	static Junction *FreeList = NULL;
	Junction *p, *newblk;

	if ( FreeList == NULL )
	{
		newblk = (Junction *)calloc(JunctionBlockAllocSize, sizeof(Junction));
		if ( newblk == NULL )
			fatal(eMsg1("out of memory while building rule '%s'",CurRule));
		for (p=newblk; p<&(newblk[JunctionBlockAllocSize]); p++)
		{
			p->p1 = (Node *)FreeList;	/* add all new Junction nodes to FreeList */
			FreeList = p;
		}
	}
	p = FreeList;
	FreeList = (Junction *)FreeList->p1;/* remove a Junction node */
	p->p1 = NULL;						/* NULL the ptr we used */

	p->ntype = nJunction;	p->visited = 0;
	p->jtype = Generic;
	p->rname = CurRule;
	p->file = CurFile;
	p->line = lex_line;
	p->fset = (set *) calloc(LL_k+1, sizeof(set));
	require(p->fset!=NULL, "cannot allocate fset in newJunction");

	return p;
}

ActionNode *
newActionNode()
{
	static ActionNode *FreeList = NULL;
	ActionNode *p, *newblk;

	if ( FreeList == NULL )
	{
		newblk = (ActionNode *)calloc(ActionBlockAllocSize, sizeof(ActionNode));
		if ( newblk == NULL )
			fatal(eMsg1("out of memory while building rule '%s'",CurRule));
		for (p=newblk; p<&(newblk[ActionBlockAllocSize]); p++)
		{
			p->next = (Node *)FreeList;	/* add all new Action nodes to FreeList */
			FreeList = p;
		}
	}
	p = FreeList;
	FreeList = (ActionNode *)FreeList->next;/* remove a Junction node */
	p->next = NULL;						/* NULL the ptr we used */
	
	p->ntype = nAction;
	return p;
}

/*
 * allocate the array of locks (1..LL_k) used to inhibit infinite recursion.
 * Infinite recursion can occur in (..)* blocks, FIRST calcs and FOLLOW calcs.
 * Therefore, we need locks on aLoopBlk, RuleBlk, EndRule nodes.
 *
 * if ( lock[k]==TRUE ) then we have been here before looking for k tokens
 * of lookahead.
 */
char *
makelocks()
{
	char *p = (char *) calloc(LL_k+1, sizeof(char));
	require(p!=NULL, "cannot allocate lock array");
	
	return p;
}
