/* Abstract syntax tree
 *
 * Macros, definitions
 * ANTLR Version 1.00
 *	Revision History: $Revision: 1.3 $
 */

#define zzastOvfChk														\
			if ( zzast_sp <= 0 )                                        \
            {                                                           \
                fprintf(stderr, zzStackOvfMsg, __FILE__, __LINE__);    	\
                exit(-1);                                               \
            }

#ifndef AST_FIELDS
#define AST_FIELDS
#endif

typedef struct _ast
{
  struct _ast *right, *down;
  AST_FIELDS} AST;


/* N o d e  a c c e s s  m a c r o s */
#define zzchild(t)		(((t)==NULL)?NULL:(t->down))
#define zzsibling(t)	(((t)==NULL)?NULL:(t->right))


/* define global variables needed by #i stack */
#define zzASTgvars												\
	AST *zzastStack[ZZAST_STACKSIZE];							\
	long zzast_sp = ZZAST_STACKSIZE;

#define zzASTVars	AST *_ast = NULL, *_sibling = NULL, *_tail = NULL
#define zzSTR		( (_tail==NULL)?(&_sibling):(&(_tail->right)) )
#define zzastCur	(zzastStack[zzast_sp])
#define zzastArg(i)	(zzastStack[zztsp-i])
#define zzastPush(p) zzastOvfChk; zzastStack[--zzast_sp] = p;
#define zzastDPush	--zzast_sp
#define zzastMARK	zztsp=zzast_sp;	/* Save state of stack */
#define zzastREL	zzast_sp=zztsp;	/* Return state of stack */
#define zzrm_ast	{zzfree_ast(*_root); _tail = _sibling = (*_root)=NULL;}

extern long zzast_sp;
extern AST *zzastStack[];

#ifdef __STDC__
void zzlink (AST **, AST **, AST **);
AST *zzastnew ();
void zzsubchild (AST **, AST **, AST **);
void zzsubroot (AST **, AST **, AST **);
void zzpre_ast (AST *, void (*)(), void (*)(), void (*)());
void zzfree_ast (AST *);
AST *zztmake (AST *, ...);
AST *zzdup_ast (AST *);
void zztfree (AST *);

#else

void zzlink ();
AST *zzastnew ();
void zzsubchild ();
void zzsubroot ();
void zzpre_ast ();
void zzfree_ast ();
AST *zztmake ();
AST *zzdup_ast ();
void zztfree ();
#endif
