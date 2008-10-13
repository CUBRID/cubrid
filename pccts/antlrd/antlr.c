/*
 * A n t l r  T r a n s l a t i o n  H e a d e r
 *
 * This file translated from: antlr.g
 *
 * (c) 1989, 1990 by Terence Parr, Hank Dietz and Will Cohen
 * Purdue University Electrical Engineering
 * ANTLR Version 1.1B
 * $Revision: 1.3 $
 */
#include <stdio.h>
#define aError(a)    dfltErr(a)
#define aError2(a)   dfltE2(a)
#define V1_0B
#include "set.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "syn.h"
#include "hash.h"
#include "generic.h"
#define aCreate(attr)
#define LEX_BUF 4000
char *scarfPAction();
void scarfComment();
void scarfAction();
void scarfQuotedTerm();
#include "dlgdef.h"
#include "attrib.h"
#include "tokens.h"
extern unsigned char err[][4];
extern char *tokens[];
static union _err { unsigned char *eset; unsigned tok; } _E;
#ifndef aStackSize
#define aStackSize 200
#endif
Attrib	aStack[aStackSize];
int		_asp=aStackSize;

#ifdef __STDC__
static void chkToken(char *, char *, char *);
#else
static void chkToken();
#endif
int grammar()
{
	RULEvars;
	BLKvars;
	Graph g;
	EnterBLK;
	EnterRule;
	Make0;
	{
	BLKvars;
	EnterBLK;
	switch ( Token )
	{
	case 7 : /* "#header" */
		Munch;
		HdrAction = calloc(strlen(LexText)+1, sizeof(char));
		require(HdrAction!=NULL, "rule grammar: cannot allocate header action");
		strcpy(HdrAction, LexText);
		if ( !lex(Action) ) { aError2(Action); goto Fail; }
		break;
	case Action :
	case NonTerminal :
	case 17 : /* "#lexaction" */
	case 18 : /* "#lexclass" */
	case 20 : /* "#errclass" */
	case 23 : /* "#token" */
		break;
	default : aError(&(err[0][0])); goto Fail;
	}
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	char *a;
	EnterBLK;
	do {
		switch ( Token )
		{
		case Action :
			a = calloc(strlen(LexText)+1, sizeof(char));
			require(a!=NULL, "rule grammar: cannot allocate action");
			strcpy(a, LexText);
			list_add(&BeforeActions, a);
			if ( !lex(Action) ) { aError2(Action); goto Fail; }
			break;
		case 17 : /* "#lexaction" */
			Make0; if (!laction()) goto Fail;
			break;
		case 18 : /* "#lexclass" */
			Make0; if (!aLexclass()) goto Fail;
			break;
		case 23 : /* "#token" */
			Make0; if (!token()) goto Fail;
			break;
		case 20 : /* "#errclass" */
			Make0; if (!error()) goto Fail;
			break;
		case NonTerminal :
			goto _L0;
		default : aError(&(err[1][0])); goto Fail;
		}
		sREL;
	} while (1);
	_L0: ;
	ExitBLK;
	}
	Make0; if (!rule()) goto Fail;
	g=aArg(3); SynDiag = (Junction *) aArg(3).left;
	Make0;
	{
	BLKvars;
	EnterBLK;
	do {
		switch ( Token )
		{
		case NonTerminal :
			Make0; if (!rule()) goto Fail;
			if ( aArg(1).left!=NULL ) {g.right = NULL; g = Or(g, aArg(1));}
			break;
		case 18 : /* "#lexclass" */
			Make0; if (!aLexclass()) goto Fail;
			break;
		case 23 : /* "#token" */
			Make0; if (!token()) goto Fail;
			break;
		case 20 : /* "#errclass" */
			Make0; if (!error()) goto Fail;
			break;
		case Action :
		case Eof :
		case 17 : /* "#lexaction" */
			goto _L1;
		default : aError(&(err[2][0])); goto Fail;
		}
		sREL;
	} while (1);
	_L1: ;
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	char *a;
	EnterBLK;
	do {
		if ( Token==Action ) {
			a = calloc(strlen(LexText)+1, sizeof(char));
			require(a!=NULL, "rule grammar: cannot allocate action");
			strcpy(a, LexText);
			list_add(&AfterActions, a);
			if ( !lex(Action) ) { aError2(Action); goto Fail; }
		}
		else if ( Token==17 ) {
			Make0; if (!laction()) goto Fail;
		}
		else if ( Token==20 ) {
			Make0; if (!error()) goto Fail;
		}
		else goto _L2;
		sREL;
	} while (1);
	_L2: ;
	if (!( Token==Eof )) {aError(&(err[3][0])); goto Fail;}
	ExitBLK;
	}
	if ( !lex(Eof) ) { aError2(Eof); goto Fail; }
	SUCCESS;
Fail: ;
	CannotContinue=TRUE;
	FAILURE;
}

int rule()
{
	RULEvars;
	BLKvars;
	RuleEntry *q; Junction *p; Graph r; int f, l; ECnode *e;
	char *pdecl=NULL, *ret=NULL, *a;
	EnterBLK;
	EnterRule;
	q=NULL;
	if ( hash_get(Rname, LexText)!=NULL ) {
		warn(eMsg1("duplicate rule definition: '%s'",LexText))
		CannotContinue=TRUE;
	}
	else
	{
		q = (RuleEntry *)hash_add(Rname,
		LexText,
		(Entry *)newRuleEntry(LexText));
		CurRule = q->str;
	}
	CurRuleNode = q;
	f = CurFile; l = lex_line;
	NumRules++;
	if ( !lex(NonTerminal) ) { aError2(NonTerminal); goto Fail; }
	Make0;
	{
	BLKvars;
	EnterBLK;
	switch ( Token )
	{
	case 10 : /* "!" */
		Munch;
		if ( q!=NULL ) q->noAST = TRUE;
		break;
	case 11 : /* "\<" */
	case PassAction :
	case 13 : /* "\>" */
	case QuotedTerm :
	case 15 : /* ":" */
		break;
	default : aError(&(err[4][0])); goto Fail;
	}
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	if ( Token==11 || Token==PassAction ) {
		Make0;
		{
		BLKvars;
		EnterBLK;
		if ( Token==11 ) {
			Munch;
		}
		else if (!( Token==PassAction )) {aError(&(err[5][0])); goto Fail;}
		ExitBLK;
		}
		pdecl = calloc(strlen(LexText)+1, sizeof(char));
		require(pdecl!=NULL, "rule rule: cannot allocate param decl");
		strcpy(pdecl, LexText);
		CurParmDef = pdecl;
		if ( !lex(PassAction) ) { aError2(PassAction); goto Fail; }
	}
	else if (!( Token==13 || Token==QuotedTerm || Token==15 )) {aError(&(err[6][0])); goto Fail;}
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	EnterBLK;
	if ( Token==13 ) {
		Munch;
		ret = calloc(strlen(LexText)+1, sizeof(char));
		require(ret!=NULL, "rule rule: cannot allocate ret type");
		strcpy(ret, LexText);
		CurRetDef = ret;
		if ( !lex(PassAction) ) { aError2(PassAction); goto Fail; }
	}
	else if (!( Token==QuotedTerm || Token==15 )) {aError(&(err[7][0])); goto Fail;}
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	if ( Token==QuotedTerm ) {
		if ( q!=NULL ) q->egroup=strdup(LexText);
		if ( !lex(QuotedTerm) ) { aError2(QuotedTerm); goto Fail; }
	}
	else if (!( Token==15 )) {aError(&(err[8][0])); goto Fail;}
	ExitBLK;
	}
	if ( GenEClasseForRules && q!=NULL ) {
		e = newECnode;
		require(e!=NULL, "cannot allocate error class node");
		if ( q->egroup == NULL ) {a = q->str; a[0] = toupper(a[0]);}
		else a = q->egroup;
		if ( Tnum( a ) == 0 )
		{
			e->tok = addTname( a );
			list_add(&eclasses, (char *)e);
			if ( q->egroup == NULL ) a[0] = tolower(a[0]);
			/* refers to itself */
			list_add(&(e->elist), strdup(q->str));
		}
		else {
			warn(eMsg1("default errclass for '%s' would conflict with token/errclass",a));
			if ( q->egroup == NULL ) a[0] = tolower(a[0]);
			free(e);
		}
	}
	BlkLevel++;
	if ( !lex(15) ) { aError2(15); goto Fail; }
	Make0; if (!block()) goto Fail;
	r = makeBlk(aArg(7));
	((Junction *)r.left)->jtype = RuleBlk;
	if ( q!=NULL ) ((Junction *)r.left)->rname = q->str;
	((Junction *)r.left)->file = f;
	((Junction *)r.left)->line = l;
	((Junction *)r.left)->pdecl = pdecl;
	((Junction *)r.left)->ret = ret;
	((Junction *)r.left)->lock = makelocks();
	p = newJunction();	/* add EndRule Node */
	((Junction *)r.right)->p1 = (Node *)p;
	r.right = (Node *) p;
	p->jtype = EndRule;
	p->lock = makelocks();
	((Junction *)r.left)->end = p;
	if ( q!=NULL ) q->rulenum = NumRules;
	aArg(7) = r;
	--BlkLevel;
	if ( !lex(16) ) { aError2(16); goto Fail; }
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	switch ( Token )
	{
	case Action :
		a = calloc(strlen(LexText)+1, sizeof(char));
		require(a!=NULL, "rule rule: cannot allocate error action");
		strcpy(a, LexText);
		((Junction *)r.left)->erraction = a;
		if ( !lex(Action) ) { aError2(Action); goto Fail; }
		break;
	case Eof :
	case NonTerminal :
	case 17 : /* "#lexaction" */
	case 18 : /* "#lexclass" */
	case 20 : /* "#errclass" */
	case 23 : /* "#token" */
		break;
	default : aError(&(err[9][0])); goto Fail;
	}
	ExitBLK;
	}
	if ( q==NULL ) aArg(0).left = NULL; else aArg(0) = aArg(7);
	CurRuleNode = NULL;
	SUCCESS;
Fail: ;
	CannotContinue=TRUE;
	FAILURE;
}

int laction()
{
	RULEvars;
	BLKvars;
	char *a;
	EnterBLK;
	EnterRule;
	if ( !lex(17) ) { aError2(17); goto Fail; }
	a = calloc(strlen(LexText)+1, sizeof(char));
	require(a!=NULL, "rule laction: cannot allocate action");
	strcpy(a, LexText);
	list_add(&LexActions, a);
	if ( !lex(Action) ) { aError2(Action); goto Fail; }
	SUCCESS;
Fail: ;
	FAILURE;
}

int aLexclass()
{
	RULEvars;
	BLKvars;
	EnterBLK;
	EnterRule;
	if ( !lex(18) ) { aError2(18); goto Fail; }
	lexclass(strdup(LexText));
	if ( !lex(TokenTerm) ) { aError2(TokenTerm); goto Fail; }
	SUCCESS;
Fail: ;
	FAILURE;
}

int error()
{
	RULEvars;
	BLKvars;
	char *t=NULL; ECnode *e; int go=1; TermEntry *p;
	EnterBLK;
	EnterRule;
	if ( !lex(20) ) { aError2(20); goto Fail; }
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	if ( Token==TokenTerm ) {
		t=strdup(LexText);
		if ( !lex(TokenTerm) ) { aError2(TokenTerm); goto Fail; }
	}
	else if ( Token==QuotedTerm ) {
		t=strdup(LexText);
		if ( !lex(QuotedTerm) ) { aError2(QuotedTerm); goto Fail; }
	}
	else {aError(&(err[10][0])); goto Fail;}
	ExitBLK;
	}
	e = newECnode;
	require(e!=NULL, "cannot allocate error class node");
	e->lexclass = CurrentLexClass;
	if ( Tnum( (t=StripQuotes(t)) ) == 0 )
	{
		if ( hash_get(Texpr, t) != NULL )
		warn(eMsg1("errclass name conflicts with regular expression  '%s'",t));
		e->tok = addTname( t );
		require((p=(TermEntry *)hash_get(Tname, t)) != NULL,
		"hash table mechanism is broken");
		p->errclassname = 1;	/* entry is errclass name, not token */
		list_add(&eclasses, (char *)e);
	}
	else
	{
	warn(eMsg1("redefinition of errclass or conflict w/token '%s'; ignored",t));
	free( e );
	go=0;
}
	if ( !lex(21) ) { aError2(21); goto Fail; }
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	if ( Token==NonTerminal ) {
		if ( go ) t=strdup(LexText);
		if ( !lex(NonTerminal) ) { aError2(NonTerminal); goto Fail; }
	}
	else if ( Token==TokenTerm ) {
		if ( go ) t=strdup(LexText);
		if ( !lex(TokenTerm) ) { aError2(TokenTerm); goto Fail; }
	}
	else if ( Token==QuotedTerm ) {
		if ( go ) t=strdup(LexText);
		if ( !lex(QuotedTerm) ) { aError2(QuotedTerm); goto Fail; }
	}
	else {aError(&(err[11][0])); goto Fail;}
	ExitBLK;
	}
	if ( go ) list_add(&(e->elist), t);
	Make0;
	{
	BLKvars;
	EnterBLK;
	while ( Token==NonTerminal || Token==QuotedTerm || Token==TokenTerm )
	{
		Make0;
		{
		BLKvars;
		;
		EnterBLK;
		if ( Token==NonTerminal ) {
			if ( go ) t=strdup(LexText);
			if ( !lex(NonTerminal) ) { aError2(NonTerminal); goto Fail; }
		}
		else if ( Token==TokenTerm ) {
			if ( go ) t=strdup(LexText);
			if ( !lex(TokenTerm) ) { aError2(TokenTerm); goto Fail; }
		}
		else if ( Token==QuotedTerm ) {
			if ( go ) t=strdup(LexText);
			if ( !lex(QuotedTerm) ) { aError2(QuotedTerm); goto Fail; }
		}
		else {aError(&(err[12][0])); goto Fail;}
		ExitBLK;
		}
		if ( go ) list_add(&(e->elist), t);
		sREL;
	}
	if (!( Token==22 )) {aError(&(err[13][0])); goto Fail;}
	ExitBLK;
	}
	if ( !lex(22) ) { aError2(22); goto Fail; }
	SUCCESS;
Fail: ;
	FAILURE;
}

int token()
{
	RULEvars;
	BLKvars;
	char *t=NULL, *e=NULL, *a=NULL;
	EnterBLK;
	EnterRule;
	if ( !lex(23) ) { aError2(23); goto Fail; }
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	switch ( Token )
	{
	case TokenTerm :
		t=strdup(LexText);
		if ( !lex(TokenTerm) ) { aError2(TokenTerm); goto Fail; }
		break;
	case Action :
	case Eof :
	case NonTerminal :
	case QuotedTerm :
	case 17 : /* "#lexaction" */
	case 18 : /* "#lexclass" */
	case 20 : /* "#errclass" */
	case 23 : /* "#token" */
		break;
	default : aError(&(err[14][0])); goto Fail;
	}
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	switch ( Token )
	{
	case QuotedTerm :
		e=strdup(LexText);
		if ( !lex(QuotedTerm) ) { aError2(QuotedTerm); goto Fail; }
		break;
	case Action :
	case Eof :
	case NonTerminal :
	case 17 : /* "#lexaction" */
	case 18 : /* "#lexclass" */
	case 20 : /* "#errclass" */
	case 23 : /* "#token" */
		break;
	default : aError(&(err[15][0])); goto Fail;
	}
	ExitBLK;
	}
	Make0;
	{
	BLKvars;
	;
	EnterBLK;
	switch ( Token )
	{
	case Action :
		a = calloc(strlen(LexText)+1, sizeof(char));
		require(a!=NULL, "rule token: cannot allocate action");
		strcpy(a, LexText);
		if ( !lex(Action) ) { aError2(Action); goto Fail; }
		break;
	case Eof :
	case NonTerminal :
	case 17 : /* "#lexaction" */
	case 18 : /* "#lexclass" */
	case 20 : /* "#errclass" */
	case 23 : /* "#token" */
		break;
	default : aError(&(err[16][0])); goto Fail;
	}
	ExitBLK;
	}
	chkToken(t, e, a);
	SUCCESS;
Fail: ;
	CannotContinue=TRUE;
	FAILURE;
}

int block()
{
	RULEvars;
	BLKvars;
	Graph g, b;
	EnterBLK;
	EnterRule;
	Make0; if (!alt()) goto Fail;
	b = g = aArg(1);
	Make0;
	{
	BLKvars;
	EnterBLK;
	while ( Token==24 )
	{
		Munch;
		Make0; if (!alt()) goto Fail;
		g = Or(g, aArg(2));
		sREL;
	}
	if (!( Token==16 || Token==22 || Token==27 )) {aError(&(err[17][0])); goto Fail;}
	ExitBLK;
	}
	aArg(0) = b;
	SUCCESS;
Fail: ;
	CannotContinue=TRUE;
	FAILURE;
}

int alt()
{
	RULEvars;
	BLKvars;
	int n=0; Graph g; g.left=g.right=NULL;
	EnterBLK;
	EnterRule;
	Make0;
	{
	BLKvars;
	EnterBLK;
	do {
		switch ( Token )
		{
		case Action :
		case NonTerminal :
		case PassAction :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 26 : /* "\(" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			Make0; if (!element()) goto Fail;
			n++; g = Cat(g, aArg(1));
			break;
		case 16 : /* ";" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 27 : /* "\)" */
			goto _L3;
		default : aError(&(err[18][0])); goto Fail;
		}
		sREL;
	} while (1);
	_L3: ;
	ExitBLK;
	}
	if ( n == 0 ) g = emptyAlt();
	aArg(0) = g;
	SUCCESS;
Fail: ;
	CannotContinue=TRUE;
	FAILURE;
}

int element()
{
	RULEvars;
	BLKvars;
	TokNode *p; RuleRefNode *q;
	EnterBLK;
	EnterRule;
	switch ( Token )
	{
	case TokenTerm :
		aArg(0) = buildToken(LexText);
		if ( !lex(TokenTerm) ) { aError2(TokenTerm); goto Fail; }
		Make0;
		{
		BLKvars;
		p = (TokNode *) ((Junction *)aRet.left)->p1;
		EnterBLK;
		switch ( Token )
		{
		case 25 : /* "^" */
			if ( !lex(25) ) { aError2(25); goto Fail; }
			p->astnode=ASTroot;
			break;
		case Action :
		case NonTerminal :
		case PassAction :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			p->astnode=ASTchild;
			break;
		case 10 : /* "!" */
			Munch;
			p->astnode=ASTexclude;
			break;
		default : aError(&(err[19][0])); goto Fail;
		}
		ExitBLK;
		}
		break;
	case QuotedTerm :
		aArg(0) = buildToken(LexText);
		if ( !lex(QuotedTerm) ) { aError2(QuotedTerm); goto Fail; }
		Make0;
		{
		BLKvars;
		p = (TokNode *) ((Junction *)aRet.left)->p1;
		EnterBLK;
		switch ( Token )
		{
		case 25 : /* "^" */
			if ( !lex(25) ) { aError2(25); goto Fail; }
			p->astnode=ASTroot;
			break;
		case Action :
		case NonTerminal :
		case PassAction :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			p->astnode=ASTchild;
			break;
		case 10 : /* "!" */
			Munch;
			p->astnode=ASTexclude;
			break;
		default : aError(&(err[20][0])); goto Fail;
		}
		ExitBLK;
		}
		break;
	case NonTerminal :
		aArg(0) = buildRuleRef(LexText);
		if ( !lex(NonTerminal) ) { aError2(NonTerminal); goto Fail; }
		Make0;
		{
		BLKvars;
		EnterBLK;
		switch ( Token )
		{
		case 10 : /* "!" */
			Munch;
			q = (RuleRefNode *) ((Junction *)aRet.left)->p1;
			q->astnode=ASTexclude;
			break;
		case Action :
		case NonTerminal :
		case 11 : /* "\<" */
		case PassAction :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			break;
		default : aError(&(err[21][0])); goto Fail;
		}
		ExitBLK;
		}
		Make0;
		{
		BLKvars;
		EnterBLK;
		switch ( Token )
		{
		case 11 : /* "\<" */
		case PassAction :
			Make0;
			{
			BLKvars;
			EnterBLK;
			if ( Token==11 ) {
				Munch;
			}
			else if (!( Token==PassAction )) {aError(&(err[22][0])); goto Fail;}
			ExitBLK;
			}
			addParm(((Junction *)aRet.left)->p1, LexText);
			if ( !lex(PassAction) ) { aError2(PassAction); goto Fail; }
			break;
		case Action :
		case NonTerminal :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			break;
		default : aError(&(err[23][0])); goto Fail;
		}
		ExitBLK;
		}
		Make0;
		{
		BLKvars;
		char *a; RuleRefNode *rr=(RuleRefNode *) ((Junction *)aRet.left)->p1;
		EnterBLK;
		switch ( Token )
		{
		case 13 : /* "\>" */
			if ( !lex(13) ) { aError2(13); goto Fail; }
			a = calloc(strlen(LexText)+1, sizeof(char));
			require(a!=NULL, "rule element: cannot allocate assignment");
			strcpy(a, LexText);
			rr->assign = a;
			if ( !lex(PassAction) ) { aError2(PassAction); goto Fail; }
			break;
		case Action :
		case NonTerminal :
		case PassAction :
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			break;
		default : aError(&(err[24][0])); goto Fail;
		}
		ExitBLK;
		}
		break;
	case Action :
		aArg(0) = buildAction(LexText,action_file,action_line);
		if ( !lex(Action) ) { aError2(Action); goto Fail; }
		break;
	case 26 : /* "\(" */
		BlkLevel++;
		if ( !lex(26) ) { aError2(26); goto Fail; }
		Make0; if (!block()) goto Fail;
		aArg(0) = aArg(2); --BlkLevel;
		if ( !lex(27) ) { aError2(27); goto Fail; }
		Make0;
		{
		BLKvars;
		EnterBLK;
		switch ( Token )
		{
		case 28 : /* "\*" */
			Munch;
			aRet = makeLoop(aRet);
			break;
		case 29 : /* "\+" */
			Munch;
			aRet = makePlus(aRet);
			break;
		case Action :
		case NonTerminal :
		case PassAction :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
			aRet = makeBlk(aRet);
			break;
		default : aError(&(err[25][0])); goto Fail;
		}
		ExitBLK;
		}
		Make0;
		{
		BLKvars;
		;
		EnterBLK;
		switch ( Token )
		{
		case PassAction :
			addParm(((Junction *)aRet.left)->p1, LexText);
			if ( !lex(PassAction) ) { aError2(PassAction); goto Fail; }
			break;
		case Action :
		case NonTerminal :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			break;
		default : aError(&(err[26][0])); goto Fail;
		}
		ExitBLK;
		}
		break;
	case 21 : /* "\{" */
		BlkLevel++;
		if ( !lex(21) ) { aError2(21); goto Fail; }
		Make0; if (!block()) goto Fail;
		aArg(0) = makeOpt(aArg(2)); --BlkLevel;
		if ( !lex(22) ) { aError2(22); goto Fail; }
		Make0;
		{
		BLKvars;
		;
		EnterBLK;
		switch ( Token )
		{
		case PassAction :
			addParm(((Junction *)aRet.left)->p1, LexText);
			if ( !lex(PassAction) ) { aError2(PassAction); goto Fail; }
			break;
		case Action :
		case NonTerminal :
		case 13 : /* "\>" */
		case QuotedTerm :
		case 15 : /* ":" */
		case 16 : /* ";" */
		case TokenTerm :
		case 21 : /* "\{" */
		case 22 : /* "\}" */
		case 24 : /* "\|" */
		case 26 : /* "\(" */
		case 27 : /* "\)" */
		case 28 : /* "\*" */
		case 29 : /* "\+" */
			break;
		default : aError(&(err[27][0])); goto Fail;
		}
		ExitBLK;
		}
		break;
	case 15 : /* ":" */
		Munch;
		warn(eMsg1("missing ';' on rule %s", CurRule));
		CannotContinue=TRUE;
		break;
	case 28 : /* "\*" */
		Munch;
		warn("don't you want a ')' with that '*'?"); CannotContinue=TRUE;
		break;
	case 29 : /* "\+" */
		Munch;
		warn("don't you want a ')' with that '+'?"); CannotContinue=TRUE;
		break;
	case 13 : /* "\>" */
		Munch;
		warn("'>' can only appear after a nonterminal"); CannotContinue=TRUE;
		break;
	case PassAction :
		Munch;
		warn("[...] out of context 'rule > [...]'");
		CannotContinue=TRUE;
		break;
	default : aError(&(err[28][0])); goto Fail;
	}
	SUCCESS;
Fail: ;
	CannotContinue=TRUE;
	FAILURE;
}

/* semantics of #token */
static void
chkToken(t,e,a)
char *t, *e, *a;
{
	if ( t==NULL && e==NULL ) {			/* none found */
		warn("#token requires at least token name or rexpr");
	}
	else if ( t!=NULL && e!=NULL ) {	/* both found */
		Tlink(t, e);
		if ( a!=NULL ) {
			if ( hasAction(e) ) {
				warn(eMsg1("redefinition of action for %s; ignored",e));
			}
			else setHasAction(e, a);
		}
	}
	else if ( t!=NULL ) {				/* only one found */
		if ( Tnum( t ) == 0 ) addTname( t );
		else {
			warn(eMsg1("redefinition of token %s; ignored",t));
		}
		if ( a!=NULL ) {
			warn(eMsg1("action cannot be attached to a token name (%s); ignored",t));
		}
	}
	else if ( e!=NULL ) {
		if ( Tnum( e ) == 0 ) addTexpr( e );
		else {
			if ( hasAction(e) ) {
				warn(eMsg1("redefinition of action for %s; ignored",e));
			}
			else if ( a==NULL ) {
				warn(eMsg1("redefinition of expr %s; ignored",e));
			}
		}
		if ( a!=NULL ) setHasAction(e, a);
	}
}
