/*
 * misc.c
 *
 * $Revision: 1.3 $
 * Manage tokens, regular expressions.
 * Print methods for debugging
 * Compute follow lists onto tail ends of rules.
 *
 * The following functions are visible:
 *
 *		int		addTname(char *);		Add token name
 *		int		addTexpr(char *);		Add token expression
 *		int		Tnum(char *);			Get number of expr/token
 *		void	Tlink(char *, char *);	Link a name with an expression
 *		int		hasAction(expr);		Does expr already have action assigned?
 *		void	setHasAction(expr);		Indicate that expr now has an action
 *		Entry	*newEntry(char *,int);	Create new table entry with certain size
 *		void	list_add(ListNode **list, char *e)
 *		void	list_apply(ListNode *list, void (*f)())
 *		void	lexclass(char *m);		switch to new/old lexical class
 *		void	lexmode(int i);			switch to old lexical class i
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"

static int tsize=TSChunk;		/* size of token str arrays */

				/* T o k e n  M a n i p u l a t i o n */

/*
 * add token 't' to the TokenStr/Expr array.  Make more room if necessary.
 * 't' is either an expression or a token name.
 *
 * There is only one TokenStr array, but multiple ExprStr's.  Therefore,
 * for each lex class (element of lclass) we must extend the ExprStr array.
 * ExprStr's and TokenStr are always all the same size.
 *
 * Also, there is a Texpr hash table for each automaton.
 */
static void
Ttrack(t)
char *t;
{
	if ( TokenNum >= tsize )	/* terminal table overflow? */
	{
		char **p;
		int i, more,j;

		more = TSChunk * (1 + ((TokenNum-tsize) / TSChunk));
		tsize += more;
/* fprintf(stderr, "TokenNum %d, new tsize is %d\n", TokenNum, tsize); */
		TokenStr = (char **) realloc(TokenStr, tsize*sizeof(char *));
		require(TokenStr != NULL, "Ttrack: can't extend TokenStr");
		for (i=0; i<NumLexClasses; i++)
		{
	          lclass[i].exprs = (char **)
     		  realloc(lclass[i].exprs, tsize*sizeof(char *));
		  require(lclass[i].exprs != NULL, "Ttrack: can't extend ExprStr");
/* bug. line used to be:
		  for (p= &lclass[i].exprs[tsize-more],i=1; i<=more; i++) *p++ = NULL;
*/
		  for (p= &lclass[i].exprs[tsize-more],j=1; j<=more; j++) *p++ = NULL;
		}
		for (p= &TokenStr[tsize-more],i=1; i<=more; i++) *p++ = NULL;
		lexmode( CurrentLexClass ); /* reset ExprStr in case table moved */
	}
	if ( *t == '"' ) ExprStr[TokenNum] = t;
	else TokenStr[TokenNum] = t;
}

static Expr *
newExpr(e)
char *e;
{
	Expr *p = (Expr *) calloc(1, sizeof(Expr));
	require(p!=NULL, "newExpr: cannot alloc Expr node");

	p->expr = e;
	p->lclass = CurrentLexClass;
	return p;
}

/* switch to lexical class/mode m.  This amounts to creating a new
 * lex mode if one does not already exist and making ExprStr point
 * to the correct char string array.  We must also switch Texpr tables.
 *
 * BTW, we need multiple ExprStr arrays because more than one automaton
 * may have the same label for a token, but with different expressions.
 * We need to track an expr for each automaton.  If we disallowed this
 * feature, only one ExprStr would be required.
 */
void
lexclass(m)
char *m;
{
	int i;
	TermEntry *p;

	if ( hash_get(Tname, m) != NULL )
	{
		warn(eMsg1("lexclass name conflicts with token/errclass label '%s'",m));
	}
	/* does m already exist? */
	i = LexClassIndex(m);
	if ( i != -1 ) {lexmode(i); return;}
	/* must make new one */
	NumLexClasses++;
	CurrentLexClass = NumLexClasses-1;
	require(NumLexClasses<=MaxLexClasses, "number of allowable lexclasses exceeded\nIncrease MaxLexClasses in generic.h and recompile all C files");
	lclass[CurrentLexClass].class = m;
	lclass[CurrentLexClass].exprs = (char **) calloc(tsize, sizeof(char *));
	require(lclass[CurrentLexClass].exprs!=NULL,
			"lexclass: cannot allocate ExprStr");
	lclass[CurrentLexClass].htable = newHashTable();
	ExprStr = lclass[CurrentLexClass].exprs;
	Texpr = lclass[CurrentLexClass].htable;
	/* define EOF for each automaton */
	p = newTermEntry( "\"@\"" );
	p->token = EofToken;
	hash_add(Texpr, "\"@\"", (Entry *)p);
	list_add(&ExprOrder, newExpr("\"@\""));
	ExprStr[EofToken] = "\"@\"";
}

void
lexmode(i)
int i;
{
	require(i<NumLexClasses, "lexmode: invalid mode");
	ExprStr = lclass[i].exprs;
	Texpr = lclass[i].htable;
	CurrentLexClass = i;
}

/* return index into lclass array of lexical class. return -1 if nonexistent */
int
LexClassIndex(class)
char *class;
{
	int i;

	for (i=0; i<NumLexClasses; i++)
	{
		if ( strcmp(lclass[i].class, class) == 0 ) return i;
	}
	return -1;
}

int
hasAction(expr)
char *expr;
{
	TermEntry *p;
	require(expr!=NULL, "hasAction: invalid expr");

	p = (TermEntry *) hash_get(Texpr, expr);
	require(p!=NULL, eMsg1("hasAction: expr '%s' doesn't exist",expr));
	return (p->action!=NULL);
}

void
setHasAction(expr,action)
char *expr, *action;
{
	TermEntry *p;
	require(expr!=NULL, "setHasAction: invalid expr");

	p = (TermEntry *) hash_get(Texpr, expr);
	require(p!=NULL, eMsg1("setHasAction: expr '%s' doesn't exist",expr));
	p->action = action;
}

/*
 * Add a token name.  Return the token number associated with it.  If it already
 * exists, then return the token number assigned to it.
 *
 * Track the order in which tokens are found so that the DLG output maintains
 * that order.  It also lets us map token numbers to strings.
 */
int
addTname(token)
char *token;
{
	TermEntry *p;
	require(token!=NULL, "addTname: invalid token name");

	if ( (p=(TermEntry *)hash_get(Tname, token)) != NULL ) return p->token;
	p = newTermEntry( token );
	Ttrack( p->str );
	p->token = TokenNum++;
	hash_add(Tname, token, (Entry *)p);
	return p->token;
}

/*
 * Add a token expr.  Return the token number associated with it.  If it already
 * exists, then return the token number assigned to it.
 */
int
addTexpr(expr)
char *expr;
{
	TermEntry *p;
	require(expr!=NULL, "addTexpr: invalid regular expression");

	if ( (p=(TermEntry *)hash_get(Texpr, expr)) != NULL ) return p->token;
	p = newTermEntry( expr );
	Ttrack( p->str );
	/* track the order in which they occur */
	list_add(&ExprOrder, newExpr(p->str));
	p->token = TokenNum++;
	hash_add(Texpr, expr, (Entry *)p);
	return p->token;
}

/* return the token number of 'term'.  Return 0 if no 'term' exists */
int
Tnum(term)
char *term;
{
	TermEntry *p;
	require(term!=NULL, "Tnum: invalid terminal");
	
	if ( *term=='"' ) p = (TermEntry *) hash_get(Texpr, term);
	else p = (TermEntry *) hash_get(Tname, term);
	if ( p == NULL ) return 0;
	else return p->token;
}

/* associate a Name with an expr.  If both have been already assigned
 * token numbers, then an error is reported.  Add the token or expr
 * that has not been added if no error.  This 'represents' the #token
 * ANTLR pseudo-op.  If both have not been defined, define them both
 * linked to same token number.
 */
void
Tlink(token,expr)
char *token, *expr;
{
	TermEntry *p, *q;
	require(token!=NULL && expr!=NULL, "Tlink: invalid token name and/or expr");

	p = (TermEntry *) hash_get(Tname, token);
	q = (TermEntry *) hash_get(Texpr, expr);
	if ( p != NULL && q != NULL )	/* both defined */
	{
		warn( eMsg2("token name %s and rexpr %s already defined; ignored",
					token, expr) );
		return;
	}
	if ( p==NULL && q==NULL )		/* both not defined */
	{
		int t = addTname( token );
		q = newTermEntry( expr );
		hash_add(Texpr, expr, (Entry *)q);
		q->token = t;
		ExprStr[t] = q->str;
		/* track the order in which they occur */
		list_add(&ExprOrder, newExpr(q->str));
		return;
	}
	if ( p != NULL )				/* one is defined, one is not */
	{
		q = newTermEntry( expr );
		hash_add(Texpr, expr, (Entry *)q);
		q->token = p->token;
		ExprStr[p->token] = q->str;	/* both expr and token str defined now */
		list_add(&ExprOrder, newExpr(q->str));
	}
	else							/* trying to associate name with expr here*/
	{
		p = newTermEntry( token );
		hash_add(Tname, token, (Entry *)p);
		p->token = q->token;
		TokenStr[p->token] = p->str;/* both expr and token str defined now */
	}
}

/*
 * Given a string, this function allocates and returns a pointer to a
 * hash table record of size 'sz' whose "str" pointer is reset to a position
 * in the string table.
 */
Entry *
newEntry(text,sz)
char *text;
int sz;
{
	Entry *p;
	require(text!=NULL, "new: NULL terminal");
	
	if ( (p = (Entry *) calloc(1,sz)) == 0 )
	{
		fatal("newEntry: out of memory for terminals\n");
		exit(1);
	}
	p->str = strdup(text);
	
	return(p);
}

/*
 * add an element to a list.
 *
 * Any non-empty list has a sentinel node whose 'elem' pointer is really
 * a pointer to the last element.  (i.e. length(list) = #elemIn(list)+1).
 * Elements are appended to the list.
 */
void
list_add(list,e)
ListNode **list;
char *e;
{
	ListNode *p, *tail;
	require(e!=NULL, "list_add: attempting to add NULL list element");

	p = newListNode;
	require(p!=NULL, "list_add: cannot alloc new list node");
	p->elem = e;
	if ( *list == NULL )
	{
		ListNode *sentinel = newListNode;
		require(sentinel!=NULL, "list_add: cannot alloc sentinel node");
		*list=sentinel;
		sentinel->next = p;
		sentinel->elem = (char *)p;		/* set tail pointer */
	}
	else								/* find end of list */
	{
		tail = (ListNode *) (*list)->elem;	/* get tail pointer */
		tail->next = p;
		(*list)->elem = (char *) p;		/* reset tail */
	}
}

void
list_apply(list,f)
ListNode *list;
void (*f)();
{
	ListNode *p;
	require(f!=NULL, "list_apply: NULL function to apply");

	if ( list == NULL ) return;
	for (p = list->next; p!=NULL; p=p->next) (*f)( p->elem );
}

			/* F O L L O W  C y c l e  S t u f f */
		
/* make a key based upon (rulename, computation, k value).
 * Computation values are 'i'==FIRST, 'o'==FOLLOW.
 */
char *
Fkey(rule,computation,k)
char *rule;
int computation;
int k;
{
	static char key[MaxRuleName+2+1];
	int i;
	
	if ( k > 255 ) 
		fatal("k>255 is too big for this implementation of ANTLR!\n");
	if ( (i=strlen(rule)) > MaxRuleName )
		fatal( eMsgd("rule name > max of %d\n", MaxRuleName) );
	strcpy(key,rule);
	key[i] = computation;
	key[i+1] = (char) ((unsigned int) k);
	key[i+2] = '\0';
	return key;
}

/* Push a rule onto the kth FOLLOW stack */
void
FoPush(rule,k)
char *rule;
int k;
{
	RuleEntry *r;
	require(rule!=NULL, "FoPush: tried to push NULL rule");
	require(k<=LL_k,	"FoPush: tried to access non-existent stack");

	/*fprintf(stderr, "FoPush(%s)\n", rule);*/
	r = (RuleEntry *) hash_get(Rname, rule);
	if ( r == NULL ) {fatal( eMsg1("rule %s must be defined but isn't", rule) );}
	if ( FoStack[k] == NULL )		/* Does the kth stack exist yet? */
	{
		/*fprintf(stderr, "allocating FoStack\n");*/
		FoStack[k] = (int *) calloc(FoStackSize, sizeof(int));
		require(FoStack[k]!=NULL, "FoPush: cannot allocate FOLLOW stack\n");
	}
	if ( FoTOS[k] == NULL )
	{
		FoTOS[k]=FoStack[k];
		*(FoTOS[k]) = r->rulenum;
	}
	else
	{
#ifdef MEMCHK
		require(valid(FoStack[k]), "FoPush: invalid FoStack");
#endif
		if ( FoTOS[k] >= &(FoStack[k][FoStackSize-1]) )
			fatal( eMsgd("exceeded max depth of FOLLOW recursion (%d)\n",
						FoStackSize) );
		require(FoTOS[k]>=FoStack[k],
				eMsg1("FoPush: FoStack stack-ptr is playing out of its sandbox",
					  rule));
		++(FoTOS[k]);
		*(FoTOS[k]) = r->rulenum;
	}
	{
		/*
		int *p;
		fprintf(stderr, "FoStack[k=%d]:\n", k);
		for (p=FoStack[k]; p<=FoTOS[k]; p++)
		{
			fprintf(stderr, "\t%s\n", RulePtr[*p]->rname);
		}
		*/
	}
}

/* Pop one rule off of the FOLLOW stack.  TOS ptr is NULL if empty. */
void
FoPop(k)
int k;
{
	require(k<=LL_k, "FoPop: tried to access non-existent stack");
	/*fprintf(stderr, "FoPop\n");*/
	require(FoTOS[k]>=FoStack[k]&&FoTOS[k]<=&(FoStack[k][FoStackSize-1]),
			"FoPop: FoStack stack-ptr is playing out of its sandbox");
	if ( FoTOS[k] == FoStack[k] ) FoTOS[k] = NULL;
	else (FoTOS[k])--;
}

/* Compute FOLLOW cycle.
 * Mark all FOLLOW sets for rules in cycle as incomplete.
 * Then, save cycle on the cycle list (Cycles) for later resolution.
 * The Cycle is stored in the form:
 *		(head of cycle==croot, rest of rules in cycle==cyclicDep)
 *
 * e.g. (Fo means "FOLLOW of", "-->" means requires or depends on)
 *
 *		Fo(x)-->Fo(a)-->Fo(b)-->Fo(c)-->Fo(x)
 *										   ^----Infinite recursion (cycle)
 *
 * the cycle would be: x -> {a,b,c} or stored as (x,{a,b,c}).  Fo(x) depends
 * on the FOLLOW of a,b, and c.  The root of a cycle is always complete after
 * Fo(x) finishes.  Fo(a,b,c) however are not.  It turns out that all rules
 * in a FOLLOW cycle have the same FOLLOW set.
 */
void
RegisterCycle(rule,k)
char *rule;
int k;
{
	CacheEntry *f;
	Cycle *c;
	int *p;
	RuleEntry *r;
	require(rule!=NULL, "RegisterCycle: tried to register NULL rule");
	require(k<=LL_k,	"RegisterCycle: tried to access non-existent stack");

	/*fprintf(stderr, "RegisterCycle(%s)\n", rule);*/
	/* Find cycle start */
	r = (RuleEntry *) hash_get(Rname, rule);
	require(r!=NULL,eMsg1("rule %s must be defined but isn't", rule));
	require(FoTOS[k]>=FoStack[k]&&FoTOS[k]<=&(FoStack[k][FoStackSize-1]),
			eMsg1("RegisterCycle(%s): FoStack stack-ptr is playing out of its sandbox",
				  rule));
/*	if ( FoTOS[k]<FoStack[k]||FoTOS[k]>&(FoStack[k][FoStackSize-1]) )
	{
		fprintf(stderr, "RegisterCycle(%s): FoStack stack-ptr is playing out of its sandbox\n",
						rule);
		fprintf(stderr, "RegisterCycle: sp==0x%x out of bounds 0x%x...0x%x\n",
						FoTOS[k], FoStack[k], &(FoStack[k][FoStackSize-1]));
		exit(-1);
	}
*/
#ifdef MEMCHK
	require(valid(FoStack[k]), "RegisterCycle: invalid FoStack");
#endif
	for (p=FoTOS[k]; *p != r->rulenum && p >= FoStack[k]; --p) {;}
	require(p>=FoStack[k], "RegisterCycle: FoStack is screwed up beyond belief");
	if ( p == FoTOS[k] ) return;	/* don't worry about cycles to oneself */
	
	/* compute cyclic dependents (rules in cycle except head) */
	c = newCycle;
	require(c!=NULL, "RegisterCycle: couldn't alloc new cycle");
	c->cyclicDep = empty;
	c->croot = *p++;		/* record root of cycle */
	for (; p<=FoTOS[k]; p++)
	{
		/* Mark all dependent rules as incomplete */
		f = (CacheEntry *) hash_get(Fcache, Fkey(RulePtr[*p]->rname,'o',k));
		if ( f==NULL )
		{
			f = newCacheEntry( Fkey(RulePtr[*p]->rname,'o',k) );
			hash_add(Fcache, Fkey(RulePtr[*p]->rname,'o',k), (Entry *)f);
		}
		f->incomplete = TRUE;
		
		set_orel(*p, &(c->cyclicDep)); /* mark rule as dependent of croot */
	}
	list_add(&(Cycles[k]), (char *)c);

	/*
	{
		unsigned e;
		set a = set_dup(c->cyclicDep);

		fprintf(stderr, "Cycle is:");
		for (; !set_nil(a); set_rm(e, a))
		{
			e = set_int(a);
			printf(" %s", RulePtr[e]->rname);
		}
		fprintf(stderr, "\n");
		set_free(a);
	}
	*/
	/*
	{	ListNode *p;
		fprintf(stderr, "Cycle list is now:\n");
		for (p = Cycles[k]->next; p!=NULL; p=p->next)
		{
			fprintf(stderr, "%s -->", RulePtr[c->croot]->rname);
			s_fprT(stderr, c->cyclicDep);
			fprintf(stderr, "\n");
		}
	}
	*/
}

/* make all rules in cycle complete
 *
 * while ( some set has changed ) do
 *		for each cycle do
 *			if degree of FOLLOW set for croot > old degree then
 *				update all FOLLOW sets for rules in cyclic dependency
 *				change = TRUE
 *			endif
 *		endfor
 * endwhile
 */
void
ResolveFoCycles(k)
int k;
{
	ListNode *p, *q;
	Cycle *c;
	int changed = 1;
	CacheEntry *f,*g;
	int r,d,i;
	
	/*fprintf(stderr, "Resolving following cycles for %d\n", k);*/
	while ( changed )
	{
		changed = 0;
		i = 0;
		for (p = Cycles[k]->next; p!=NULL; p=p->next)
		{
			c = (Cycle *) p->elem;
			/*fprintf(stderr, "cycle %d: %s -->", i++, RulePtr[c->croot]->rname);*/
			/*s_fprT(stderr, c->cyclicDep);*/
			/*fprintf(stderr, "\n");*/
			f = (CacheEntry *)
					hash_get(Fcache, Fkey(RulePtr[c->croot]->rname,'o',k));
			require(f!=NULL, eMsg1("FOLLOW(%s) must be in cache but isn't", RulePtr[c->croot]->rname) );
			if ( (d=set_deg(f->fset)) > c->deg )
			{
				/*fprintf(stderr, "Fo(%s) has changed\n", RulePtr[c->croot]->rname);*/
				changed = 1;
				c->deg = d;		/* update cycle FOLLOW set degree */
				while ( !set_nil(c->cyclicDep) )
				{
					r = set_int(c->cyclicDep);
					set_rm(r, c->cyclicDep);
					/*fprintf(stderr, "updating Fo(%s)\n", RulePtr[r]->rname);*/
					g = (CacheEntry *)
							hash_get(Fcache, Fkey(RulePtr[r]->rname,'o',k));
					require(g!=NULL, eMsg1("FOLLOW(%s) must be in cache but isn't", RulePtr[r]->rname) );
					set_orin(&(g->fset), f->fset);
					g->incomplete = FALSE;
				}
			}
		}
		if ( i == 1 ) changed = 0;	/* if only 1 cycle, no need to repeat */
	}
	/* kill Cycle list */
	for (q = Cycles[k]->next; q != NULL; q=p)
	{
		p = q->next;
		set_free( ((Cycle *)q->elem)->cyclicDep );
		free(q);
	}
	free( Cycles[k] );
	Cycles[k] = NULL;
}


			/* P r i n t i n g  S y n t a x  D i a g r a m s */

static void
pBlk(q,btype)
Junction *q;
int btype;
{
	int k;
	Junction *alt;

	q->end->visited = TRUE;
	if ( btype == aLoopBegin )
	{
		require(q->p2!=NULL, "pBlk: invalid ()* block");
		PRINT(q->p1);
		alt = (Junction *)q->p2;
		PRINT(alt->p1);
		if ( PrintAnnotate )
		{
			printf(" /* Opt ");
			k = 1;
			while ( !set_nil(alt->fset[k]) )
			{
				s_fprT(stdout, alt->fset[k]);
				if ( k++ == LL_k ) break;
				if ( !set_nil(alt->fset[k]) ) printf(", ");
			}
			printf(" */\n");
		}
		return;
	}
	for (alt=q; alt != NULL; alt= (Junction *) alt->p2 )
	{
		if ( alt->p1 != NULL ) PRINT(alt->p1);
		if ( PrintAnnotate )
		{
			printf( " /* [%d] ", alt->altnum);
			k = 1;
			while ( !set_nil(alt->fset[k]) )
			{
				s_fprT(stdout, alt->fset[k]);
				if ( k++ == LL_k ) break;
				if ( !set_nil(alt->fset[k]) ) printf(", ");
			}
			if ( alt->p2 == NULL && btype == aOptBlk )
				printf( " (optional branch) */\n");
			else printf( " */\n");
		}
		if ( alt->p2 != NULL && btype != aOptBlk )
		{
			printf(" |");
		}
	}
	q->end->visited = FALSE;
}

/* How to print out a junction */
void
pJunc(q)
Junction *q;
{
	require(q!=NULL, "pJunc: NULL node");
	require(q->ntype==nJunction, "pJunc: not junction");
	
	if ( q->visited == TRUE ) return;
	q->visited = TRUE;
	switch ( q->jtype )
	{
		case aSubBlk :
			if ( PrintAnnotate ) First(q, 1, q->jtype);
			printf("("); pBlk(q,q->jtype); printf(" )\n");
			if ( PrintAnnotate ) freeBlkFsets(q);
			if ( q->end->p1 != NULL ) PRINT(q->end->p1);
			break;
		case aOptBlk :
			if ( PrintAnnotate ) First(q, 1, q->jtype);
			printf("{"); pBlk(q,q->jtype); printf(" }");
			if ( PrintAnnotate ) freeBlkFsets(q);
			if ( q->end->p1 != NULL ) PRINT(q->end->p1);
			break;
		case aLoopBegin :
			if ( PrintAnnotate ) First(q, 1, q->jtype);
			printf("("); pBlk(q,q->jtype); printf(" )*");
			if ( PrintAnnotate ) freeBlkFsets(q);
			if ( q->end->p1 != NULL ) PRINT(q->end->p1);
			break;
		case aLoopBlk :
			if ( PrintAnnotate ) First(q, 1, q->jtype);
			pBlk(q,q->jtype);
			if ( PrintAnnotate ) freeBlkFsets(q);
			break;
		case aPlusBlk :
			if ( PrintAnnotate ) First(q, 1, q->jtype);
			printf("("); pBlk(q,q->jtype); printf(" )+");
			if ( PrintAnnotate ) freeBlkFsets(q);
			if ( q->end->p1 != NULL ) PRINT(q->end->p1);
			break;
		case EndBlk :
			break;
		case RuleBlk :
			printf( "\n%s :", q->rname);
			PRINT(q->p1);
			if ( q->p2 != NULL ) PRINT(q->p2);
			break;
		case Generic :
			if ( q->p1 != NULL ) PRINT(q->p1);
			q->visited = FALSE;
			if ( q->p2 != NULL ) PRINT(q->p2);
			break;
		case EndRule :
			printf( " ;\n");
			break;
	}
	q->visited = FALSE;
}

/* How to print out a rule reference node */
void
pRuleRef(p)
RuleRefNode *p;
{
	require(p!=NULL, "pRuleRef: NULL node");
	require(p->ntype==nRuleRef, "pRuleRef: not rule ref node");
	
	printf( " %s", p->text);
	PRINT(p->next);
}

/* How to print out a terminal node */
void
pToken(p)
TokNode *p;
{
	require(p!=NULL, "pToken: NULL node");
	require(p->ntype==nToken, "pToken: not token node");

	printf( " %s",
		(TokenStr[p->token]==NULL)?ExprStr[p->token]:TokenStr[p->token]);
	PRINT(p->next);
}

/* How to print out a terminal node */
void
pAction(p)
ActionNode *p;
{
	require(p!=NULL, "pAction: NULL node");
	require(p->ntype==nAction, "pAction: not action node");
	
	PRINT(p->next);
}

					/* F i l l  F o l l o w  L i s t s */

/*
 * Search all rules for all rule reference nodes, q to rule, r.
 * Add q->next to follow list dangling off of rule r.
 * i.e.
 *
 *		r: -o-R-o-->o--> Ptr to node following rule r in another rule
 *					|
 *					o--> Ptr to node following another reference to r.
 *
 * This is the data structure employed to avoid FOLLOW set computation.  We
 * simply compute the FIRST (reach) of the EndRule Node which follows the
 * list found at the end of all rules which are referenced elsewhere.  Rules
 * not invoked by other rules have no follow list (r->end->p1==NULL).
 * Generally, only start symbols are not invoked by another rule.
 *
 * Note that this mechanism also gives a free cross-reference mechanism.
 *
 * The entire syntax diagram is layed out like this:
 *
 * SynDiag
 *	|
 *	v
 *	o-->R1--o
 *	|
 *	o-->R2--o
 *	|
 *	...
 *	|
 *	o-->Rn--o
 *
 */
void
FoLink(p)
Node *p;
{
	RuleEntry *q;
	Junction *j = (Junction *) p;
	RuleRefNode *r = (RuleRefNode *) p;

	if ( p==NULL ) return;
	require(p->ntype>=1 && p->ntype<=NumJuncTypes,	"FoLink: invalid diagram node");
	switch ( p->ntype )
	{
		case nJunction :
			if ( j->visited ) return;
			if ( j->jtype == EndRule ) return;
			j->visited = TRUE;
			FoLink( j->p1 );
			FoLink( j->p2 );
			j->visited = FALSE;
			return;
		case nRuleRef :
			q = (RuleEntry *) hash_get(Rname, r->text);
			if ( q == NULL )
			{
				warnNoFL( eMsg1("rule %s not defined",r->text) );
			}
			else
			{
				if ( r->parms!=NULL && RulePtr[q->rulenum]->pdecl==NULL )
				{
					warnFL( eMsg1("rule %s accepts no parameter(s)", r->text),
							FileStr[r->file], r->line );
				}
				if ( r->parms==NULL && RulePtr[q->rulenum]->pdecl!=NULL )
				{
					warnFL( eMsg1("rule %s requires parameter(s)", r->text),
							FileStr[r->file], r->line );
				}
				if ( r->assign!=NULL && RulePtr[q->rulenum]->ret==NULL )
				{
					warnFL( eMsg1("rule %s yields no return value(s)", r->text),
							FileStr[r->file], r->line );
				}
				if ( r->assign==NULL && RulePtr[q->rulenum]->ret!=NULL )
				{
					warnFL( eMsg1("rule %s returns a value(s)", r->text),
							FileStr[r->file], r->line );
				}
				if ( !r->linked )
				{
					addFoLink(	r->next, r->rname, RulePtr[q->rulenum] );
					r->linked = TRUE;
				}
			}
			FoLink( r->next );
			return;
		case nToken :
			FoLink( ((TokNode *)p)->next );
			return;
		case nAction :
			FoLink( ((ActionNode *)p)->next );
			return;
		default :
			fatal("invalid node type");
	}
}

/*
 * Add a reference to the end of a rule.
 *
 * 'r' points to the RuleBlk node in a rule.  r->end points to the last node
 * (EndRule jtype) in a rule.
 *
 * Initial:
 *		r->end --> 	o
 *
 * After:
 *		r->end --> 	o-->o--> Ptr to node following rule r in another rule
 *						|
 *						o--> Ptr to node following another reference to r.
 *
 * Note that the links are added to the head of the list so that r->end->p1
 * always points to the most recently added follow-link.  At the end, it should
 * point to the last reference found in the grammar (starting from the 1st rule).
 */
void addFoLink(p,rname,r)
Node *p;
char *rname;
Junction *r;
{
	Junction *j;
	require(r!=NULL,				"addFoLink: incorrect rule graph");
	require(r->end!=NULL,			"addFoLink: incorrect rule graph");
	require(r->end->jtype==EndRule,	"addFoLink: incorrect rule graph");
	require(p!=NULL,				"addFoLink: NULL FOLLOW link");

	j = newJunction();
	j->rname = rname;			/* rname on follow links point to target rule */
	j->p1 = p;					/* link to other rule */
	j->p2 = (Node *) r->end->p1;/* point to head of list */
	r->end->p1 = (Node *) j;	/* reset head to point to new node */
}

void
GenCrossRef(p)
Junction *p;
{
	set a;
	Junction *j;
	RuleEntry *q;
	unsigned e;
	require(p!=NULL, "GenCrossRef: why are you passing me a null grammar?");

	printf("Cross Reference:\n\n");
	a = empty;
	for (; p!=NULL; p = (Junction *)p->p2)
	{
		printf("Rule %11s referenced by {", p->rname);
		/* make a set of rules for uniqueness */
		for (j = (Junction *)(p->end)->p1; j!=NULL; j = (Junction *)j->p2)
		{
			q = (RuleEntry *) hash_get(Rname, j->rname);
			require(q!=NULL, "GenCrossRef: FoLinks are screwed up");
			set_orel(q->rulenum, &a);
		}
		for (; !set_nil(a); set_rm(e, a))
		{
			e = set_int(a);
			printf(" %s", RulePtr[e]->rname);
		}
		printf(" }\n");
	}
	set_free( a );
}
