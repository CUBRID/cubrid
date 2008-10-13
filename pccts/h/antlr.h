/*
 * a n t l r . h
 *
 * Define all of the stack setup and manipulation of $i, #i variables.
 *
 *	Notes:
 *		The type 'Attrib' must be defined before entry into this .h file.
 *
 * ANTLR 1.00
 * Terence John Parr 1989, 1990, 1991
 * Purdue University
 *
 *	Revision History: $Revision: 1.4 $
 */

#include "mtdef.h"

/* can make this a power of 2 for more efficient lookup */
#ifndef ZZLEXBUFSIZE
#define ZZLEXBUFSIZE	2000
#endif

#define zzOvfChk	\
            if ( zzasp <= 0 )                                           \
            {                                                           \
                zzoverflow(); 						\
                goto fail;						\
            }

#ifndef ZZA_STACKSIZE
#define ZZA_STACKSIZE	800
#endif
#ifndef ZZAST_STACKSIZE
#define ZZAST_STACKSIZE	800
#endif

#ifdef LL_K
#define LOOKAHEAD				\
	PUBLIC_TLS long zztokenLA[LL_K];			\
	PUBLIC_TLS char zztextLA[LL_K][ZZLEXBUFSIZE];	\
	PUBLIC_TLS long zzlap = 0;				\
        PUBLIC_TLS long zzlineLA[LL_K], zzcolumnLA[LL_K];
#else
#define LOOKAHEAD				\
	PUBLIC_TLS long zztoken;
#endif


#ifndef zzcr_ast
#define zzcr_ast(ast,attr,tok,text)
#endif

/* The zzStackOvfMsg error message is repeated often and therefore only one
 * copy is made to save space.
 */

#define ANTLR_INFO							\
	Attrib zzempty_attr() {static Attrib a; return a;}		\
	Attrib zzconstr_attr(_tok, _text) long _tok; char *_text;	\
		{Attrib a; zzcr_attr((&a),_tok,_text); return a;}	\
	PUBLIC_TLS long zzasp=ZZA_STACKSIZE;					\
	const char zzStackOvfMsg[]=					\
	    "fatal: attrib/AST stack overflow %s(%d)!\n"; 		\
	PUBLIC_TLS Attrib zzaStack[ZZA_STACKSIZE];	\
void zzoverflow() \
{                                                           \
     fprintf(stderr, zzStackOvfMsg, __FILE__, __LINE__);	\
     zzasp = 0;						\
} \
static long zzmatch_fail(long _t) \
{ 	\
	    if (LA(1)!=_t ) { return 1;} \
	    zzOvfChk; 			\
	    --zzasp; 			\
	    zzcr_attr(&(zzaStack[zzasp]),LA(1),LATEXT(1));	\
            return 0;			\
      fail: return 1;		\
} ANTLR_INFO_TAIL

#ifndef LL_K
#define ANTLR_INFO_TAIL
#else
#define ANTLR_INFO_TAIL \
PUBLIC_TLS char *zztextend[LL_K];		\
PUBLIC_TLS char *zzlextextend;		\
    \
void zzconsume2() \
{ 		\
EXTERN_TLS char * zzendexpr; \
    zzcolumnLA[0] = zzcolumnLA[1]; \
    zzcolumnLA[1] = zzbegcol; \
    zzgettok(); \
    zzlineLA[0] = zzlineLA[1];  \
    zzlineLA[1] = zzline; \
    zztextend[0] = zztextend[1];  \
    zztextend[1] = zzendexpr; \
    zzlap = (zzlap+1)&(1);	  \
    zzlextext = &(zztextLA[zzlap][0]); \
    zzlextextend = zztextend[0]; \
}
#endif

#ifdef LL_K
#define zzPrimeLookAhead  { long _i; \
			    zzasp = ZZA_STACKSIZE; zzlap = 0; \
			    for(_i=0; _i<LL_K; _i++)          \
                              { zzlineLA[_i] = zzline; zzcolumnLA[_i] = 0; } \
			    for(_i=1;_i<=LL_K; _i++)		\
			      { zzCONSUME; } \
			    zzlap = 0; \
			  }
#else
#define zzPrimeLookAhead  zzgettok();
#endif

#ifdef LL_K
#define zzenterANTLRf(f)					     \
		zzlextext = zztextLA[0]; \
		zzrdfunc( f ); zzPrimeLookAhead;
#define zzenterANTLR(f)							\
		zzlextext = zztextLA[0]; \
		zzrdstream( f ); zzPrimeLookAhead;
#else
#define zzenterANTLRf(f)				\
		{PRIVATE_TLS char zztoktext[ZZLEXBUFSIZE];	\
		zzlextext = zztoktext; zzrdfunc( f ); zzPrimeLookAhead;}
#define zzenterANTLR(f)							\
		{PRIVATE_TLS char zztoktext[ZZLEXBUFSIZE];	\
		zzlextext = zztoktext; zzrdstream( f ); zzPrimeLookAhead;}
#endif

#define ANTLR(st, f)   zzbufsize = ZZLEXBUFSIZE; zzenterANTLR(f);  st; ++zzasp;
#define ANTLRf(st, f)  zzbufsize = ZZLEXBUFSIZE; zzenterANTLRf(f); st; ++zzasp;

#ifdef LL_K
#define zztext		(zztextLA[zzlap])
#else
#define zztext		zzlextext
#endif


					/* A r g u m e n t  A c c e s s */

#define zzaCur			(zzaStack[zzasp])
#define zzaRet			(*zzaRetPtr)
#define zzaArg(v,n)		zzaStack[v-n]

#define zzaPush(_v)		{zzOvfChk; zzaStack[--zzasp] = _v;}
#ifndef zzd_attr
#define zzREL(t)		zzasp=(t);	/* Restore state of stack */
#else
#define zzREL(t)		for (; zzasp<(t); zzasp++)		  \
                                    { zzd_attr(&(zzaStack[zzasp])); }
#endif

#define zzmatch(_t)	if ( zzmatch_fail(zzMissTok = _t) ) goto fail;

#ifdef GENAST
#define zzRULE	\
			unsigned long *zzMissSet = NULL; long zzMissTok=0; zzASTVars
#else
#define zzRULE	\
			unsigned long *zzMissSet = NULL; long zzMissTok=0
#endif

#ifdef GENAST
#define zzBLOCK(i)	long i = --zzasp; long zztsp = zzast_sp
#define zzEXIT(i)	zzREL(i); zzastREL; zzastPush(*_root);
#define zzLOOP(i)	zzREL(i); zzastREL
#else
#define zzBLOCK(i)	long i = --zzasp
#define zzEXIT(i)	zzREL(i)
#define zzLOOP(i)	zzREL(i)
#endif

#ifdef zzdef0
#define zzMake0			{zzOvfChk;  zzdef0(&(zzaStack[zzasp]))}
#else
#define zzMake0			{zzOvfChk;  }
#endif

#define zzFAIL(_s)		{zzMissSet = _s; goto fail;}

#ifdef LL_K
#if LL_K == 2
#define zzCONSUME zzconsume2()
#else
#define zzCONSUME { long k; for(k=0; k<LL_K-1; k++) \
                  zzcolumnLA[k] = zzcolumnLA[k+1]; } \
                  zzcolumnLA[LL_K-1] = zzbegcol; \
                  zzgettok(); \
                  { long x; for(x=0; x<LL_K-1; x++) zzlineLA[x] = zzlineLA[x+1]; } \
		  zzlineLA[LL_K-1] = zzline; \
		  zzlap = (zzlap+1)&(LL_K-1);	  \
		  zzlextext = &(zztextLA[zzlap][0]);
#endif
#else
#define zzCONSUME	zzgettok();
#endif

#ifdef LL_K

#if LL_K == 2
#define LA(i)		zztokenLA[(zzlap+i-1)& 1]
#define LATEXT(i)	( zztextLA[(zzlap+i-1)& 1] )
#else
#define LA(i)		zztokenLA[(zzlap+i-1)&(LL_K-1)]
#define LATEXT(i)	( zztextLA[(zzlap+i-1)&(LL_K-1)] )
#endif

#else
#define LA(i)		zztoken
#define LATEXT(i)	zztext
#endif

				/* E x t e r n  D e f s */

extern const char zzStackOvfMsg[];
extern const char *zztokens[];
extern char *zzedecode (unsigned long *p);
EXTERN_TLS long zzasp;
EXTERN_TLS long zzbufsize;
EXTERN_TLS Attrib zzaStack[];
extern Attrib zzempty_attr ();
#ifdef __STDC__
extern Attrib zzconstr_attr (long, char *);
#else
extern Attrib zzconstr_attr ();
#endif

#ifdef LL_K
EXTERN_TLS long zztokenLA[];
EXTERN_TLS char zztextLA[][ZZLEXBUFSIZE];
EXTERN_TLS long zzlap;
#else
EXTERN_TLS long zztoken;
#endif

void zzresynch (unsigned long *wd, unsigned long mask);

void zzsyn (const char *text, long tok, const char *egroup,
	    unsigned long *eset, long etok);
