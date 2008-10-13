/*
 * a t t r i b . h
 *
 * Define all of the stack setup and manipulation of $i variables.
 *
 *	Notes:
 *
 *		The type 'Attrib' must be defined before entry into this .h file.
 *		OR, the follow default attrib types have been defined for use:
 *
 *	Available Default Attribute Types:
 *
 *			D_Text -- Attributes are Text buffers
 *				Must #def D_Text and #def D_TextSize to max size of
 *				string to be used as attribute.
 *
 *			D_String -- Attributes are String Pointers
 *				Must #def D_String
 *
 *			D_Integer -- Attributes are signed 32-bit integers
 *				Must #def D_Integer
 *
 * ANTLR 1.0B
 * Terence John Parr (c) 1989, 1990
 * Purdue University
 *
 * Enhanced Version				-- July 1989
 * Added $$,$rule,EnterRule		-- Sept 1989
 * Added aDestroy()				-- Nov  1989
 * Make $0 before entering blk	-- Jan  1990
 * Changed aCreate()			-- Feb	1990
 * $Revision: 1.3 $
 */

#ifndef _ATTRIB_H_
#define _ATTRIB_H_

/* D e f a u l t  S t a c k  A t t r ' s  D e f i n i t i o n s */
#ifdef D_Text
#ifndef D_TextSize
#define D_TextSize	30
#endif
typedef struct { char text[D_TextSize]; } Attrib;
#define aCreate(a)		strncpy((a)->text, LexText, D_TextSize-1);
#endif

#ifdef D_String
typedef char *Attrib;
#define aCreate(a)
#endif

#ifdef D_Integer
typedef long Attrib;
#define aCreate(a)		*(a) = atoi(LexText);
#endif

/*
 * The following error message is repeated often and therefore only one
 * copy is made to save space.  Here, we actually allocate space which
 * I don't like to do in an include file (ick).
 */
static char argStackOvfMsg[]="Antlr Attribute Stack OverFlow %s(%d)!\n";

/*
 * A t t r i b u t e  C o n v e r s i o n
 *
 * The user must specify the type of and how to create ANTLR attributes.
 *
 * The type is always specified with 'Attrib'.
 *
 * The attributes that are associated with TERMINAL elements of rule
 * definitions ($i variables) must be a function of LexText and Token
 * (filled by dlg).  The user must define a function or macro that
 * has the following format:
 *
 *		aCreate(attr)
 *		Attrib *attr;
 *		{
 *			*attr = attribute for LexText/Token;
 *		}
 *
 * aCreate() is called from lex() with 'aCreate(&(aStack[_asp]))'.
 *
 * aCreate() can also be a macro.
 */

#ifndef DESTROY
#define Restore(t)		_asp=(t);
#else
#define Restore(t)		WipeOut(t);
#endif

#define WipeOut(t)		for (; _asp<(t); _asp++) { aDestroy(&(aStack[_asp])); }
			
#define RULEvars		Attrib *aRetPtr
#define BLKvars         int _tasp
#define sMARK           _tasp=_asp			/* Save state of stack */
#define sREL            Restore(_tasp)		/* Return state of stack */

#define EnterRule		aRetPtr = (&(aArg(0)));
#define EnterBLK		sMARK;
#define ExitBLK			sREL;
#define Make0			aOvfChk; --_asp;
#define aOvfChk															\
            if ( _asp == 0 )                                            \
            {                                                           \
                fprintf(stderr, argStackOvfMsg, __FILE__, __LINE__);    \
                exit(-1);                                               \
            }
#define aPush(v)														\
			aOvfChk; aStack[--_asp] = v;

/* R u l e  T e r m i n a t i o n */
#define SUCCESS {sREL; return(1);}				/* Leave $0 & Return */
#define FAILURE {Restore(_tasp+1); return(0);}	/* Kill $0 & Return */

/*
 * A r g u m e n t  A c c e s s
 *
 * $i is converted to 'aStack[_tasp-i]'
 * $0 is return value of current block (sometimes $0 is the return of rule)
 * $$, $r, $rule are always the return value of the current RULE!
 */
#define aRet				(*aRetPtr)
#define aArg(n)             aStack[_tasp-n]

/*
 * This Munches the current input token by pushing it on the stack
 * and getting the next token from the input stream.
 * --_asp is out in front because aCreate() could be a macro.
 */
#define Munch   	MakeAttr; GetToken();
#define MakeAttr	aOvfChk; --_asp; aCreate(&(aStack[_asp]));

/* S t a n d a r d  B e f o r e / A f t e r  S t a r t  R u l e  M a c r o s */
#define enterANTLR(f)					\
    SetLexInputStream( f );             \
    advance();                          \
    GetToken();                         \

#define exitANTLR		sREL;

#define ANTLRi(start, a, f)				\
    {	BLKvars;						\
		sMARK;							\
		enterANTLR(f);					\
    	aPush(a);						\
		start();                        \
		exitANTLR;						\
	}

#define ANTLR(start, f)					\
    {	BLKvars;						\
		sMARK;							\
		enterANTLR(f);					\
		Make0;							\
		start();                        \
		exitANTLR;						\
	}

/* E x t e r n  S t a c k  D e f s */
extern int  _asp;
extern Attrib aStack[];
extern char *aSourceFile;

#ifdef D_aTrax
#define aTrax(f,t) fprintf(stderr, "%s(%s)\n", f, t);
#endif


#endif
