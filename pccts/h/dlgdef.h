/* dlg2.h
 * Things in scanner produced by dlg that should be visible to the outside
 * world
 *
 * Will Cohen
 * 11/15/90
 *	Revision History: $Revision: 1.4 $
 */

#include "mtdef.h"

EXTERN_TLS char *zzlextext;	/* text of most recently matched token */
EXTERN_TLS char *zzbegexpr;	/* beginning of last reg expr recogn. */
EXTERN_TLS char *zzendexpr;	/* beginning of last reg expr recogn. */
EXTERN_TLS long zzbufsize;	/* how long zzlextext is */
EXTERN_TLS long zzbegcol;	/* column that first character of token is in */
EXTERN_TLS long zzendcol;	/* column that last character of token is in */
EXTERN_TLS long zzline;		/* line current token is on */
EXTERN_TLS long zzchar;		/* character to determine next state */
EXTERN_TLS long zzlineLA[];	/* line positions of lookahead tokens */
EXTERN_TLS long zzcolumnLA[];	/* column positions of lookahead tokens */

#ifdef ZZRDFUNC_SUPPLIED
EXTERN_TLS long zzauto;
EXTERN_TLS FILE *zzstream_in;
EXTERN_TLS long (*zzfunc_in) ();
#else
/* added by jsl */
/* extern void zzrdfunc(long (*f)(); -- parenthesis mismatch -bk*/
/* what is this for??? */
#endif

/* functions */
EXTERN_TLS void (*zzerr) ();	/* pointer to error reporting function */
extern void zzadvance ();	/* ? */
extern void zzskip ();		/* erase zzlextext, look for antoher token */
extern void zzmore ();		/* keep zzlextext, look for another token */
/* added "long" to signature, jsl */
extern void zzmode (long m);	/* switch to automaton 'k' */
extern void zzrdstream ();	/* what stream to read from */
extern void zzclose_stream ();	/* close the current input stream */
extern void zzrdfunc ();	/* what function to get char from */
extern void zzgettok ();	/* get next token */
extern void zzreplchar ();	/* replace last recognized reg. expr. with
				   a character */
extern void zzreplstr ();	/* replace last recognized reg. expr. with
				   a string */
