#include <stdio.h>
#include <stdlib.h>
#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"

/*
 * $Revision: 1.3 $
*/

extern char tokens[];

/* ick! globals.  Used by permute() to track which elements of a set have been used */
static int *index;
static set *fset;
static unsigned **ftbl;
static set *constrain; /* pts into fset. constrains tToken() to 'constrain' */
static int ConstrainSearch;
static int maxk; /* set to initial k upon tree construction request */
static int TreeIncomplete;

static Tree *FreeList = NULL;

/* Do root
 * Then each sibling
 */
void preorder(tree)
Tree *tree;
{
	if ( tree == NULL ) return;
	if ( tree->down != NULL ) fprintf(stderr, " (");
	if ( tree->token == ALT ) fprintf(stderr, " J");
	else fprintf(stderr, " %s", (TokenStr[tree->token]!=NULL)?TokenStr[tree->token]:
							(ExprStr[tree->token]
							? ExprStr[tree->token]
							: ""));
	if ( tree->token==EpToken ) fprintf(stderr, "(%d)", tree->v.rk);
	preorder(tree->down);
	if ( tree->down != NULL ) fprintf(stderr, " )");
	preorder(tree->right);
}

/* build a tree (root child1 child2 ... NULL) */
#ifdef __STDC__
Tree *tmake(Tree *root, ...)
#else
Tree *tmake(va_alist)
va_dcl
#endif
{
	va_list ap;
	Tree *child, *sibling=NULL, *tail;
#ifndef __STDC__
	Tree *root;
#endif
#ifdef __STDC__
	require(root!=NULL, "tmake: NULL root impossible");
#endif

#ifdef __STDC__
	va_start(ap, root);
#else
	va_start(ap);
	root = va_arg(ap, Tree *);
#endif
	child = va_arg(ap, Tree *);
	while ( child != NULL )
	{
		if ( sibling == NULL ) sibling = tail = child;
		else {tail->right = child; tail = child;}
		child = va_arg(ap, Tree *);
	}
	root->down = sibling;
	va_end(ap);
	return root;
}

Tree *
tnode(tok)
int tok;
{
	Tree *p, *newblk;
	
	if ( FreeList == NULL )
	{
		/*fprintf(stderr, "tnode: %d more nodes\n", TreeBlockAllocSize);*/
		newblk = (Tree *)calloc(TreeBlockAllocSize, sizeof(Tree));
		if ( newblk == NULL )
		{
			fprintf(stderr, ErrHdr, FileStr[CurAmbigfile], CurAmbigline);
			fprintf(stderr, " out of memory while analyzing alts %d and %d of %s\n",
							CurAmbigAlt1,
							CurAmbigAlt2,
							CurAmbigbtype);
			exit(-1);
		}
		for (p=newblk; p<&(newblk[TreeBlockAllocSize]); p++)
		{
			p->right = FreeList;	/* add all new Tree nodes to Free List */
			FreeList = p;
		}
	}
	p = FreeList;
	FreeList = FreeList->right;		/* remove a tree node */
	p->right = NULL;				/* zero out ptrs */
	p->down = NULL;
	p->token = tok;
	return p;
}

void
tfree(t)
Tree *t;
{
	if ( t!=NULL )
	{
		t->right = FreeList;
		FreeList = t;
	}
}

/* tree duplicate */
Tree *
tdup(t)
Tree *t;
{
	Tree *u;
	
	if ( t == NULL ) return NULL;
	u = tnode(t->token);
	u->v.rk = t->v.rk;
	u->right = tdup(t->right);
	u->down = tdup(t->down);
	return u;
}

Tree *
tappend(t,u)
Tree *t, *u;
{
	Tree *w;

	/*fprintf(stderr, "tappend(");
	preorder(t); fprintf(stderr, ",");
	preorder(u); fprintf(stderr, " )\n");*/
	if ( t == NULL ) return u;
	if ( t->token == ALT && t->right == NULL ) return tappend(t->down, u);
	for (w=t; w->right!=NULL; w=w->right) {;}
	w->right = u;
	return t;
}

/* dealloc all nodes in a tree */
void
Tfree(t)
Tree *t;
{
	if ( t == NULL ) return;
	Tfree( t->down );
	Tfree( t->right );
	tfree( t );
}

/* find all children (alts) of t that require remaining_k nodes to be LL_k
 * tokens long.
 *
 * t-->o
 *     |
 *     a1--a2--...--an		<-- LL(1) tokens
 *     |   |        |
 *     b1  b2  ...  bn		<-- LL(2) tokens
 *     |   |        |
 *     .   .        .
 *     .   .        .
 *     z1  z2  ...  zn		<-- LL(LL_k) tokens
 *
 * We look for all [Ep] needing remaining_k nodes and replace with u.
 * u is not destroyed or actually used by the tree (a copy is made).
 */
Tree *
tlink(t,u,remaining_k)
Tree *t, *u;
int remaining_k;
{
	Tree *p;
	require(remaining_k!=0, "tlink: bad tree");

	if ( t==NULL ) return NULL;
	/*fprintf(stderr, "tlink: u is:"); preorder(u); fprintf(stderr, "\n");*/
	if ( t->token == EpToken && t->v.rk == remaining_k )
	{
		require(t->down==NULL, "tlink: invalid tree");
		if ( u == NULL ) return t->right;
		p = tdup( u );
		p->right = t->right;
		tfree( t );
		return p;
	}
	t->down = tlink(t->down, u, remaining_k);
	t->right = tlink(t->right, u, remaining_k);
	return t;
}

/* remove as many ALT nodes as possible while still maintaining semantics */
Tree *
tshrink(t)
Tree *t;
{
	if ( t == NULL ) return NULL;
	t->down = tshrink( t->down );
	t->right = tshrink( t->right );
	if ( t->down == NULL )
	{
		if ( t->token == ALT )
		{
			Tree *u = t->right;
			tfree(t);
			return u;			/* remove useless alts */
		}
		return t;
	}

	/* (? (ALT (? ...)) s) ==> (? (? ...) s) where s = sibling, ? = match any */
	if ( t->token == ALT && t->down->right == NULL)
	{
		Tree *u = t->down;
		u->right = t->right;
		tfree( t );
		return u;
	}
	/* (? (A (ALT t)) s) ==> (? (A t) s) where A is a token; s,t siblings */
	if ( t->token != ALT && t->down->token == ALT && t->down->right == NULL )
	{
		Tree *u = t->down->down;
		tfree( t->down );
		t->down = u;
		return t;
	}
	return t;
}

Tree *
tflatten(t)
Tree *t;
{
	if ( t == NULL ) return NULL;
	t->down = tflatten( t->down );
	t->right = tflatten( t->right );
	if ( t->down == NULL ) return t;
	
	if ( t->token == ALT )
	{
		Tree *u;
		for (u=t->down; u->right!=NULL; u=u->right) {;}	/* find tail of children */
		u->right = t->right;
		u = t->down;
		tfree( t );
		return u;
	}
	return t;
}

Tree *
tJunc(p,k,rk)
Junction *p;
int k;
set *rk;
{
	Tree *t=NULL, *u=NULL;
	Junction *alt;
	Tree *tail, *r;

#ifdef DBG_TRAV
	fprintf(stderr, "tJunc(%d): %s in rule %s\n", k,
			decodeJType[p->jtype], ((Junction *)p)->rname);
#endif
	if ( p->jtype==aLoopBlk || p->jtype==RuleBlk ||
		 p->jtype==aPlusBlk || p->jtype==aSubBlk || p->jtype==aOptBlk )
	{
		if ( p->jtype!=aSubBlk && p->jtype!=aOptBlk ) {
			require(p->lock!=NULL, "rJunc: lock array is NULL");
			if ( p->lock[k] ) return NULL;
			p->lock[k] = TRUE;
		}
		TRAV(p->p1, k, rk, tail);
		if ( p->jtype==RuleBlk ) {p->lock[k] = FALSE; return tail;}
		r = tmake(tnode(ALT), tail, NULL);
		for (alt=(Junction *)p->p2; alt!=NULL; alt = (Junction *)alt->p2)
		{
			if ( tail==NULL ) {TRAV(alt->p1, k, rk, tail); r->down = tail;}
			else
			{
				TRAV(alt->p1, k, rk, tail->right);
				/* TJP -- lost info when tail->right was NULL
				tail = tail->right;*/
				if ( tail->right != NULL ) tail = tail->right;
			}
		}
		if ( p->jtype!=aSubBlk && p->jtype!=aOptBlk ) p->lock[k] = FALSE;
		/*fprintf(stderr, "blk(%s) returns:",((Junction *)p)->rname); preorder(r); fprintf(stderr, "\n");*/
		if ( r->down == NULL ) {tfree(r); return NULL;}
		return r;
	}

	if ( p->jtype==EndRule )
	{
		if ( p->halt )						/* don't want FOLLOW here? */
		{
			set_orel(k, rk);				/* indicate this k value needed */
			t = tnode(EpToken);
			t->v.rk = k;
			return t;
		}
		require(p->lock!=NULL, "rJunc: lock array is NULL");
		if ( p->lock[k] ) return NULL;
		if ( p->p1 == NULL ) return tnode(EofToken);/* if no FOLLOW assume EOF */
		p->lock[k] = TRUE;
	}

	if ( p->p2 == NULL )
	{
		TRAV(p->p1, k, rk,t);
		if ( p->jtype==EndRule ) p->lock[k]=FALSE;
		return t;
	}
	TRAV(p->p1, k, rk, t);
	if ( p->jtype!=RuleBlk ) TRAV(p->p2, k, rk, u);
	if ( p->jtype==EndRule ) p->lock[k] = FALSE;/* unlock node */

	if ( t==NULL ) return tmake(tnode(ALT), u, NULL);
	return tmake(tnode(ALT), t, u, NULL);
}

Tree *
tRuleRef(p,k,rk_out)
RuleRefNode *p;
int k;
set *rk_out;
{ 
	int k2;
	Tree *t, *u;
	Junction *r;
	set rk, rk2;
	char save_halt;
	RuleEntry *q = (RuleEntry *) hash_get(Rname, p->text);
	
#ifdef DBG_TRAV
	fprintf(stderr, "tRuleRef: %s\n", p->text);
#endif
	if ( q == NULL )
	{
		TRAV(p->next, k, rk_out, t);/* ignore undefined rules */
		return t;
	}
	rk = rk2 = empty;
	r = RulePtr[q->rulenum];
	if ( r->lock[k] ) return NULL;
	save_halt = r->end->halt;
	r->end->halt = TRUE;		/* don't let reach fall off end of rule here */
	TRAV(r, k, &rk, t);
	r->end->halt = save_halt;
/*fprintf(stderr, "after ruleref, t is:"); preorder(t); fprintf(stderr, "\n");*/
	t = tshrink( t );
	while ( !set_nil(rk) ) {	/* any k left to do? if so, link onto tree */
		k2 = set_int(rk);
		set_rm(k2, rk);
		TRAV(p->next, k2, &rk2, u);
		t = tlink(t, u, k2);	/* any alts missing k2 toks, add u onto end */
	}
	set_orin(rk_out, rk2);		/* remember what we couldn't do */
	set_free(rk2);
	return t;
}

Tree *
tToken(p,k,rk)
TokNode *p;
int k;
set *rk;
{
	Tree *t;
/*
	require(constrain>=fset&&constrain<=&(fset[LL_k]),"tToken: constrain is not a valid set");
*/	
	constrain = &fset[maxk-k+1];

#ifdef DBG_TRAV
	fprintf(stderr, "tToken(%d): %s\n", k,
						(TokenStr[p->token]!=NULL)?TokenStr[p->token]:
						ExprStr[p->token]);
	fprintf(stderr, "constrain is:"); s_fprT(stderr, *constrain); fprintf(stderr, "\n");
#endif
	if ( ConstrainSearch )
		if ( !set_el(p->token, *constrain) )
		{
			/*fprintf(stderr, "ignoring token %s(%d)\n",
							(TokenStr[p->token]!=NULL)?TokenStr[p->token]:
							ExprStr[p->token],
							k);
			*/
			return NULL;
		}
	if ( k == 1 ) return tnode(p->token);
	TRAV(p->next, k-1, rk, t);
	if ( t == NULL )
	{
		TreeIncomplete = 1;	/* tree will be too shallow */
		return NULL;
	}
	/*fprintf(stderr, "tToken(%d)->next:",k); preorder(t); fprintf(stderr, "\n");*/
	return tmake(tnode(p->token), t, NULL);
}

Tree *
tAction(p,k,rk)
ActionNode *p;
int k;
set *rk;
{
	Tree *t;
	
	/*fprintf(stderr, "tAction\n");*/
	TRAV(p->next, k, rk, t);
	return t;
}

/* see if e exists in s as a possible input permutation (e is always a chain) */
int
tmember(e,s)
Tree *e, *s;
{
	if ( e==NULL||s==NULL ) return 0;
	/*fprintf(stderr, "tmember(");
	preorder(e); fprintf(stderr, ",");
	preorder(s); fprintf(stderr, " )\n");*/
	if ( s->token == ALT && s->right == NULL ) return tmember(e, s->down);
	if ( e->token!=s->token )
	{
		if ( s->right==NULL ) return 0;
		return tmember(e, s->right);
	}
	if ( e->down==NULL && s->down == NULL ) return 1;
	if ( tmember(e->down, s->down) ) return 1;
	if ( s->right==NULL ) return 0;
	return tmember(e, s->right);
}

/* combine (? (A t) ... (A u) ...) into (? (A t u)) */
Tree *
tleft_factor(t)
Tree *t;
{
	Tree *u, *v, *trail, *w;

	/* left-factor what is at this level */
	if ( t == NULL ) return NULL;
	for (u=t; u!=NULL; u=u->right)
	{
		trail = u;
		v=u->right;
		while ( v!=NULL )
		{
			if ( u->token == v->token )
			{
				if ( u->down!=NULL )
				{
					for (w=u->down; w->right!=NULL; w=w->right) {;}
					w->right = v->down;	/* link children together */
				}
				trail->right = v->right;		/* unlink factored node */
				tfree( v );
				v = trail->right;
			}
			else {trail = v; v=v->right;}
		}
	}
	/* left-factor what is below */
	for (u=t; u!=NULL; u=u->right) u->down = tleft_factor( u->down );
	return t;
}

/* remove the permutation p from t if present */
Tree *
trm_perm(t,p)
Tree *t, *p;
{
	/*
	fprintf(stderr, "trm_perm(");
	preorder(t); fprintf(stderr, ",");
	preorder(p); fprintf(stderr, " )\n");
	*/
	if ( t == NULL || p == NULL ) return NULL;
	if ( t->token == ALT )
	{
		t->down = trm_perm(t->down, p);
		if ( t->down == NULL ) 				/* nothing left below, rm cur node */
		{
			Tree *u = t->right;
			tfree( t );
			return trm_perm(u, p);
		}
		t->right = trm_perm(t->right, p);	/* look for more instances of p */
		return t;
	}
	if ( p->token != t->token )				/* not found, try a sibling */
	{
		t->right = trm_perm(t->right, p);
		return t;
	}
	t->down = trm_perm(t->down, p->down);
	if ( t->down == NULL ) 					/* nothing left below, rm cur node */
	{
		Tree *u = t->right;
		tfree( t );
		return trm_perm(u, p);
	}
	t->right = trm_perm(t->right, p);		/* look for more instances of p */
	return t;
}

/* add the permutation 'perm' to the LL_k sets in 'fset' */
void tcvt(fset,perm)
set *fset;
Tree *perm;
{
	if ( perm==NULL ) return;
	set_orel(perm->token, fset);
	tcvt(fset+1, perm->down);
}

/* for each element of ftbl[k], make it the root of a tree with permute(ftbl[k+1])
 * as a child.
 */
Tree *
permute(k)
int k;
{
	Tree *t, *u;
	
	if ( k>LL_k ) return NULL;
	if ( ftbl[k][index[k]] == nil ) return NULL;
	t = permute(k+1);
	if ( t==NULL&&k<LL_k )		/* no permutation left below for k+1 tokens? */
	{
		index[k+1] = 0;
		(index[k])++;			/* try next token at this k */
		return permute(k);
	}
	
	u = tmake(tnode(ftbl[k][index[k]]), t, NULL);
	if ( k == LL_k ) (index[k])++;
	return u;
}

/* Compute LL(k) trees for alts alt1 and alt2 of p.
 * function result is tree of ambiguous input permutations
 *
 * ALGORITHM may change to look for something other than LL_k size
 * trees ==> maxk will have to change.
 */
Tree *
VerifyAmbig(alt1,alt2,ft,fs,t,u,numAmbig)
Junction *alt1, *alt2;
unsigned **ft;
set *fs;						/* set of possible ambiguities */
Tree **t, **u;					/* return constrained perm sets */
int *numAmbig;					/* how many ambigs were there */
{
	set rk;
	Tree *perm, *ambig=NULL;
	int k;

	maxk = LL_k;				/* NOTE: for now, we look for LL_k */
	ftbl = ft;
	fset = fs;
	constrain = &(fset[1]);
	index = (int *) calloc(LL_k+1, sizeof(int));
	if ( index == NULL )
	{
		fprintf(stderr, ErrHdr, CurAmbigfile, CurAmbigline);
		fprintf(stderr, " out of memory while analyzing alts %d and %d of %s\n",
						CurAmbigAlt1,
						CurAmbigAlt2,
						CurAmbigbtype);
		exit(-1);
	}
	for (k=1; k<=LL_k; k++) index[k] = 0;

	rk = empty;
	ConstrainSearch = 1;	/* consider only tokens in ambig sets */

	TreeIncomplete = 0;		/* assume tree will have depth LL_k */
	TRAV(alt1->p1, LL_k, &rk, *t);
	if ( TreeIncomplete )
	{
		Tfree( *t );	/* kill if impossible to have ambig */
		*t = NULL;
	}

	TreeIncomplete = 0;
	TRAV(alt2->p1, LL_k, &rk, *u);
	if ( TreeIncomplete )
	{
		Tfree( *u );
		*u = NULL;
	}

	*t = tshrink( *t );
	*u = tshrink( *u );
	*t = tflatten( *t );	/* WARNING: do we waste time here? */
	*u = tflatten( *u );
	*t = tleft_factor( *t );
	*u = tleft_factor( *u );

	/*
	fprintf(stderr, "after shrink&flatten&lfactor:"); preorder(*t); fprintf(stderr, "\n");
	fprintf(stderr, "after shrink&flatten&lfactor:"); preorder(*u); fprintf(stderr, "\n");
	*/

	for (k=1; k<=LL_k; k++) set_clr( fs[k] );

	ambig = tnode(ALT);
	k = 0;
	while ( (perm=permute(1))!=NULL )
	{
		/*fprintf(stderr, "chk perm:"); preorder(perm); fprintf(stderr, "\n");*/
		if ( tmember(perm, *t) && tmember(perm, *u) )
		{
			/*fprintf(stderr, "ambig upon"); preorder(perm); fprintf(stderr, "\n");*/
			k++;
			perm->right = ambig->down;
			ambig->down = perm;
			tcvt(&(fs[1]), perm);
		}
		else Tfree( perm );
	}

	*numAmbig = k;
	if ( ambig->down == NULL ) {tfree(ambig); ambig = NULL;}
	free( index );
	/*fprintf(stderr, "final ambig:"); preorder(ambig); fprintf(stderr, "\n");*/
	return ambig;
}
