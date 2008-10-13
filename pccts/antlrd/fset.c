/*
 * fset.c
 *
 * Compute FIRST and FOLLOW sets.
 * $Revision: 1.3 $:w
 */

#include <stdio.h>
#include <stdlib.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"

/*
 * What tokens are k tokens away from junction q?
 *
 * Follow both p1 and p2 paths (unless RuleBlk) to collect the tokens k away from this
 * node.
 * We lock the junction according to k--the lookahead.  If we have been at this
 * junction before looking for the same, k, number of lookahead tokens, we will
 * do it again and again...until we blow up the stack.  Locks are only used on aLoopBlk,
 * RuleBlk, aPlusBlk and EndRule junctions to remove/detect infinite recursion from
 * FIRST and FOLLOW calcs.
 *
 * If p->jtype == EndRule we are going to attempt a FOLLOW.  (FOLLOWs are really defined
 * in terms of FIRST's, however).  To proceed with the FOLLOW, p->halt cannot be
 * set.  p->halt is set to indicate that a reference to the current rule is in progress
 * and the FOLLOW is not desirable.
 *
 * If we attempt a FOLLOW and find that there is no FOLLOW or REACHing beyond the EndRule
 * junction yields an empty set, replace the empty set with EOF.  No FOLLOW means that
 * only EOF can follow the current rule.  This normally occurs only on the start symbol
 * since all other rules are referenced by another rule somewhere.
 *
 * Normally, both p1 and p2 are followed.  However, checking p2 on a RuleBlk node is
 * the same as checking the next rule which is clearly incorrect.
 *
 * Cycles in the FOLLOW sense are possible.  e.g. Fo(c) requires Fo(b) which requires
 * Fo(c).  Both Fo(b) and Fo(c) are defined to be Fo(b) union Fo(c).  Let's say
 * Fo(c) is attempted first.  It finds all of the FOLLOW symbols and then attempts
 * to do Fo(b) which finds of its FOLLOW symbols.  So, we have:
 *
 *                  Fo(c)
 *                 /     \
 *              a set    Fo(b)
 *                      /     \
 *                   a set    Fo(c) .....Hmmmm..... Infinite recursion!
 *
 * The 2nd Fo(c) is not attempted and Fo(b) is left deficient, but Fo(c) is now
 * correctly Fo(c) union Fo(b).  We wish to pick up where we left off, so the fact
 * that Fo(b) terminated early means that we lack Fo(c) in the Fo(b) set already
 * laying around.  SOOOOoooo, we track FOLLOW cycles.  All FOLLOW computations are
 * cached in a hash table.  After the sequence of FOLLOWs finish, we reconcile all
 * cycles --> correct all Fo(rule) sets in the cache.
 *
 * Confused? Good! Read my MS thesis [Purdue Technical Report TR90-30].
 *
 * Also, FIRST sets are cached in the hash table.  Keys are (rulename,Fi/Fo,k).
 * Only FIRST sets, for which the FOLLOW is not included, are stored.
 */
set
rJunc(p,k,rk)
Junction *p;
int k;
set *rk;
{
	set a, b;
	require(p!=NULL,				"rJunc: NULL node");
	require(p->ntype==nJunction,	"rJunc: not junction");

	/*if ( p->jtype == RuleBlk ) fprintf(stderr, "FIRST(%s,%d) \n",((Junction *)p)->rname,k);
	else fprintf(stderr, "rJunc: %s in rule %s\n",
			decodeJType[p->jtype], ((Junction *)p)->rname);
	*/
	/* locks are valid for aLoopBlk,aPlusBlk,RuleBlk,EndRule junctions only */
	if ( p->jtype==aLoopBlk || p->jtype==RuleBlk ||
		 p->jtype==aPlusBlk || p->jtype==EndRule ) 
	{
		require(p->lock!=NULL, "rJunc: lock array is NULL");
		if ( p->lock[k] )
		{
			if ( p->jtype == EndRule )	/* FOLLOW cycle? */
			{
				/*fprintf(stderr, "FOLLOW cycle to %s: panic!\n", p->rname);*/
				RegisterCycle(p->rname, k);
			}
			return empty;
		}
		if ( p->jtype == RuleBlk && p->end->halt )	/* check for FIRST cache */
		{
			CacheEntry *q = (CacheEntry *) hash_get(Fcache, Fkey(p->rname,'i',k));
			if ( q != NULL )
			{
				set_orin(rk, q->rk);
				return set_dup( q->fset );
			}
		}
		if ( p->jtype == EndRule )		/* FOLLOW set cached already? */
		{
			CacheEntry *q = (CacheEntry *) hash_get(Fcache, Fkey(p->rname,'o',k));
			if ( q != NULL )
			{
				/*fprintf(stderr, "<->FOLLOW(%s,%d):", p->rname,k);
				s_fprT(stderr, q->fset);
				if ( q->incomplete ) fprintf(stderr, " (incomplete)");
				fprintf(stderr, "\n");
				*/
				if ( !q->incomplete )
				{
					return set_dup( q->fset );
				}
			}
		}
		p->lock[k] = TRUE;	/* This rule is busy */
	}

	a = b = empty;

	if ( p->jtype == EndRule )
	{
		if ( p->halt )								/* don't want FOLLOW here? */
		{
			p->lock[k] = FALSE;
			set_orel(k, rk);						/* indicate this k value needed */
			return empty;
		}
		FoPush(p->rname, k);						/* Attempting FOLLOW */
		if ( p->p1 == NULL ) set_orel(EofToken, &a);/* if no FOLLOW assume EOF */
		/*fprintf(stderr, "-->FOLLOW(%s,%d)\n", p->rname,k);*/
	}

	if ( p->p1 != NULL ) REACH(p->p1, k, rk, a);
	
	/* C a c h e  R e s u l t s */
	if ( p->jtype == RuleBlk && p->end->halt )		/* can save FIRST set? */
	{
		CacheEntry *q = newCacheEntry( Fkey(p->rname,'i',k) );
		/*fprintf(stderr, "Caching %s FIRST %d\n", p->rname, k);*/
		hash_add(Fcache, Fkey(p->rname,'i',k), (Entry *)q);
		q->fset = set_dup( a );
		q->rk = set_dup( *rk );
	}

	if ( p->jtype == EndRule )						/* just completed FOLLOW? */
	{
		/* Cache Follow set */
		CacheEntry *q = (CacheEntry *) hash_get(Fcache, Fkey(p->rname,'o',k));
		if ( q==NULL )
		{
			q = newCacheEntry( Fkey(p->rname,'o',k) );
			hash_add(Fcache, Fkey(p->rname,'o',k), (Entry *)q);
		}
		/*fprintf(stderr, "Caching %s FOLLOW %d\n", p->rname, k);*/
		set_orin(&(q->fset), a);
		FoPop( k );
		if ( FoTOS[k] == NULL && Cycles[k] != NULL ) ResolveFoCycles(k);
		/*
		fprintf(stderr, "<--FOLLOW(%s,%d):", p->rname, k);
		s_fprT(stderr, q->fset);
		if ( q->incomplete ) fprintf(stderr, " (incomplete)");
		fprintf(stderr, "\n");
		*/
	}
	
	if ( p->jtype != RuleBlk && p->p2 != NULL ) REACH(p->p2, k, rk, b);
	if ( p->jtype==aLoopBlk || p->jtype==RuleBlk ||
		 p->jtype==aPlusBlk || p->jtype==EndRule ) 
		p->lock[k] = FALSE;							/* unlock node */

	set_orin(&a, b);
	set_free(b);
	return a;
}

set
rRuleRef(p,k,rk_out)
RuleRefNode *p;
int k;
set *rk_out;
{
	set rk;
	Junction *r;
	int k2;
	set a, rk2, b;
	int save_halt;
	RuleEntry *q = (RuleEntry *) hash_get(Rname, p->text);
	require(p!=NULL,			"rRuleRef: NULL node");
	require(p->ntype==nRuleRef,	"rRuleRef: not rule ref");

	/*fprintf(stderr, "rRuleRef: %s\n", p->text);*/
	if ( q == NULL )
	{
		warnNoFL( eMsg1("rule %s not defined",p->text) );
		REACH(p->next, k, rk_out, a);
		return a;
	}
	rk2 = empty;
	r = RulePtr[q->rulenum];
	if ( r->lock[k] )
	{
		warnNoFL( eMsg2("infinite left-recursion to rule %s from rule %s",
						r->rname, p->rname) );
		return empty;
	}
	save_halt = r->end->halt;
	r->end->halt = TRUE;		/* don't let reach fall off end of rule here */
	rk = empty;
	REACH(r, k, &rk, a);
	r->end->halt = save_halt;
	while ( !set_nil(rk) ) {
		k2 = set_int(rk);
		set_rm(k2, rk);
		REACH(p->next, k2, &rk2, b);
		set_orin(&a, b);
		set_free(b);
	}
	set_orin(rk_out, rk2);		/* remember what we couldn't do */
	set_free(rk2);
	return a;
}

set
rToken(p,k,rk)
TokNode *p;
int k;
set *rk;
{
	set a;
	require(p!=NULL,			"rToken: NULL node");
	require(p->ntype==nToken,	"rToken: not token node");

	/*fprintf(stderr, "rToken: %s\n", (TokenStr[p->token]!=NULL)?TokenStr[p->token]:
									ExprStr[p->token]);*/
	if ( k-1 == 0 ) return set_of( p->token );
	REACH(p->next, k-1, rk, a);
	
	return a;
}

set
rAction(p,k,rk)
ActionNode *p;
int k;
set *rk;
{
	set a;
	require(p!=NULL,			"rJunc: NULL node");
	require(p->ntype==nAction,	"rJunc: not action");
	
	REACH(p->next, k, rk, a);	/* ignore actions */
	return a;
}

				/* A m b i g u i t y  R e s o l u t i o n */


static void
dumpAmbigMsg(fset)
set *fset;
{
	int i;

	fprintf(stderr, " ");
	for (i=1; i<=LL_k; i++)
	{
		if ( i>1 ) fprintf(stderr, ", ");
		if ( set_deg(fset[i]) > 3 && elevel == 1 )
		{
			int e,m;
			fprintf(stderr, "{");
			for (m=1; m<=3; m++)
			{
				e=set_int(fset[i]);
				fprintf(stderr, " %s",
					(TokenStr[e]!=NULL)?TokenStr[e]:ExprStr[e]);
				set_rm(e, fset[i]);
			}
			fprintf(stderr, " ... }");
		}
		else s_fprT(stderr, fset[i]);
	}
	fprintf(stderr, "\n");
	for (i=1; i<=LL_k; i++) set_free( fset[i] );
	free(fset);
}

void
HandleAmbiguity(alt1,alt2,jtype)
Junction *alt1, *alt2;
int jtype;
{
	unsigned **ftbl;
	set *fset, b;
	int i, numAmbig, n, n2;
	Tree *ambig, *t, *u;
	char *sub = "";

	fset = (set *) calloc(LL_k+1, sizeof(set));
	require(fset!=NULL, "cannot allocate fset");
	ftbl = (unsigned **) calloc(LL_k+1, sizeof(unsigned *));
	require(ftbl!=NULL, "cannot allocate ftbl");
	/* create constraint table and count number of possible ambiguities */
	for (n=1,i=1; i<=LL_k; i++)
	{
		b = set_and(alt1->fset[i], alt2->fset[i]);
		n *= set_deg(b);
		fset[i] = set_dup(b);
		ftbl[i] = set_pdq(b);
		set_free(b);
	}

	switch ( jtype )
	{
		case aSubBlk: sub = "of (..) "; break;
		case aOptBlk: sub = "of {..} "; break;
		case aLoopBegin: sub = "of (..)* "; break;
		case aLoopBlk: sub = "of (..)* "; break;
		case aPlusBlk: sub = "of (..)+ "; break;
		case RuleBlk: sub = "of rule "; break;
	}

	/* if all sets have degree 1 for k<LL_k, then ambig upon all permutation */
	n2 = 0;
	for (i=1; i<LL_k; i++) n2 += set_deg(alt1->fset[i])+set_deg(alt2->fset[i]);
	if ( n2==2*(LL_k-1) )
	{
		fprintf(stderr, ErrHdr, FileStr[alt1->file], alt1->line);
		if ( jtype == aLoopBegin )
			fprintf(stderr, " warning: optional path and alt(s) of (..)* ambiguous upon");
		else
			fprintf(stderr, " warning: alts %d and %d %sambiguous upon",
						alt1->altnum, alt2->altnum, sub);
		dumpAmbigMsg(fset);
		for (i=1; i<=LL_k; i++) free( ftbl[i] );
		free(ftbl);
		return;
	}

	/* in case tree construction runs out of memory, set info to make good err msg */
	CurAmbigAlt1 = alt1->altnum;
	CurAmbigAlt2 = alt2->altnum;
	CurAmbigbtype = sub;
	CurAmbigfile = alt1->file;
	CurAmbigline = alt1->line;
	
	ambig = VerifyAmbig(alt1, alt2, ftbl, fset, &t, &u, &numAmbig);
	for (i=1; i<=LL_k; i++) free( ftbl[i] );
	free(ftbl);
	/* are all things in intersection really ambigs? */
	if ( numAmbig < n )
	{
		Tree *v;

		/* remove ambig permutation from 2nd alternative to resolve ambig */
		if ( ambig!=NULL )
		{
			for (v=ambig->down; v!=NULL; v=v->right)
			{
				u = trm_perm(u, v);
			}
			/*fprintf(stderr, "after rm alt2:"); preorder(u); fprintf(stderr, "\n");*/
		}
		Tfree( t );
		alt1->ftree = tappend(alt1->ftree, u);
		alt1->ftree = tleft_factor(alt1->ftree);
	}

	if ( ambig==NULL )
	{
		for (i=1; i<=LL_k; i++) set_free( fset[i] );
		free(fset);
		return;
	}

	ambig = tleft_factor(ambig);

	fprintf(stderr, ErrHdr, FileStr[alt1->file], alt1->line);
	if ( jtype == aLoopBegin )
		fprintf(stderr, " warning: optional path and alt(s) of (..)* ambiguous upon");
	else
		fprintf(stderr, " warning: alts %d and %d %sambiguous upon",
					alt1->altnum, alt2->altnum, sub);
	if ( elevel == 3 )
	{
		preorder(ambig->down);
		fprintf(stderr, "\n");
		Tfree(ambig);
		return;
	}
	Tfree(ambig);
	dumpAmbigMsg(fset);
}

set
First(j,k,jtype)
Junction *j;
int k, jtype;
{
	Junction *alt1, *alt2;
	set a, rk, fCurBlk;
	int savek;
	require(j->ntype==nJunction, "First: non junction passed");

	/* C o m p u t e  F I R S T  s e t  w i t h  k  l o o k a h e a d */
	fCurBlk = rk = empty;
	for (alt1=j; alt1!=NULL; alt1 = (Junction *)alt1->p2)
	{
		REACH(alt1->p1, k, &rk, alt1->fset[k]);
		require(set_nil(rk), "rk != nil");
		set_orin(&fCurBlk, alt1->fset[1]);
	}

	/* D e t e c t  A m b i g u i t i e s */
	for (alt1=j; alt1!=NULL; alt1 = (Junction *)alt1->p2)
	{
		for (alt2=(Junction *)alt1->p2; alt2!=NULL; alt2 = (Junction *)alt2->p2)
		{
			savek = k;
			a = set_and(alt1->fset[k], alt2->fset[k]);
			while ( !set_nil(a) )
			{
				if ( k==LL_k )
				{
					HandleAmbiguity(alt1, alt2, jtype);
					break;
				}
				else
				{
					k++;			/* attempt ambig alts again with more lookahead */
					REACH(alt1->p1, k, &rk, alt1->fset[k]);
					require(set_nil(rk), "rk != nil");
					REACH(alt2->p1, k, &rk, alt2->fset[k]);
					require(set_nil(rk), "rk != nil");
					set_free(a);
					a = set_and(alt1->fset[k], alt2->fset[k]);
				}
			}
			set_free(a);
			k = savek;
		}
	}
	
	return fCurBlk;
}
