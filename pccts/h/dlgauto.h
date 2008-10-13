/* dlgauto.h automaton
 *	Revision History: $Revision: 1.4 $
 */

#include "mtdef.h"

PUBLIC_TLS char *zzlextext;	/* text of most recently matched token */
PUBLIC_TLS char *zzbegexpr;	/* beginning of last reg expr recogn. */
PUBLIC_TLS char *zzendexpr;	/* beginning of last reg expr recogn. */
PUBLIC_TLS long zzbufsize;	/* number of characters in zzlextext */
PUBLIC_TLS long zzbegcol = 0;	/* column that first character of token is in */
PUBLIC_TLS long zzendcol = 0;	/* column that last character of token is in */
PUBLIC_TLS long zzline = 1;	/* line current token is on */
PUBLIC_TLS long zzchar;		/* character to determine next state */
PUBLIC_TLS long zzbufovfcnt = 0;	/* token buffer overflow count */
PUBLIC_TLS long zzcharfull = 0;
#if 1
PUBLIC_TLS char *zznextpos;	/* points to next available position in zzlextext */
PUBLIC_TLS long zzclass;
#else
PRIVATE_TLS char *zznextpos;	/* points to next available position in zzlextext */
PRIVATE_TLS long zzclass;
#endif

#ifdef ZZRDFUNC_SUPPLIED
#define MAYBE_GLOBAL PUBLIC_TLS
#else
#define MAYBE_GLOBAL PRIVATE_TLS
#endif

void zzadvance ();
void zzerrstd ();
PUBLIC_TLS void (*zzerr) () = zzerrstd;	/* pointer to error reporting function */

MAYBE_GLOBAL FILE *zzstream_in;
extern long zzerr_in ();
MAYBE_GLOBAL long (*zzfunc_in) () = zzerr_in;

MAYBE_GLOBAL long zzauto = 0;
#if 1
PUBLIC_TLS long zzadd_erase;
PUBLIC_TLS char zzebuf[70];
#else
PRIVATE_TLS long zzadd_erase;
PRIVATE_TLS char zzebuf[70];
#endif

#ifdef ZZCOL
#define ZZINC (++zzendcol)
#else
#define ZZINC
#endif


#define ZZGETC_STREAM {zzchar = getc(zzstream_in); zzclass = ZZSHIFT(zzchar);}

#define ZZGETC_FUNC {zzchar = (*zzfunc_in)(); zzclass = ZZSHIFT(zzchar);}

#ifndef ZZCOPY
#define ZZCOPY	\
	/* Truncate matching buffer to size (not an error) */	\
	if (zznextpos < lastpos){				\
		*(zznextpos++) = zzchar;			\
	}else{							\
		zzbufovfcnt++;					\
	}
#endif

#ifndef ZZRDSTREAM_SUPPLIED
void
zzrdstream (f)
     FILE *f;
{
  zzbegcol = 0;			/* column that first character of token is in */
  zzendcol = 0;			/* column that last character of token is in */
  zzline = 1;			/* line current token is on */
  zzchar = 0;
  zzcharfull = 0;
  zzauto = 0;

  zzline = 1;
  zzstream_in = f;
  zzfunc_in = NULL;
}
#endif

#ifndef ZZRDFUNC_SUPPLIED
void
zzrdfunc (f)
     long (*f) ();
{
  zzbegcol = 0;			/* column that first character of token is in */
  zzendcol = 0;			/* column that last character of token is in */
  zzline = 1;			/* line current token is on */
  zzchar = 0;
  zzcharfull = 0;
  zzauto = 0;

  zzline = 1;
  zzstream_in = NULL;
  zzfunc_in = f;
}
#endif

void
zzclose_stream ()
{
  fclose (zzstream_in);
  zzstream_in = NULL;
  zzfunc_in = NULL;
}

void
zzmode (m)
     long m;
{
  /* points to base of dfa table */
  if (m < MAX_MODE)
    {
      zzauto = m;
      /* have to redo class since using different compression */
      zzclass = ZZSHIFT (zzchar);
    }
  else
    {
      sprintf (zzebuf, "Invalid automaton mode = %d ", m);
      zzerr (zzebuf);
    }
}

/* erase what is currently in the buffer, and get a new reg. expr */
void
zzskip ()
{
  zzadd_erase = 1;
}

/* don't erase what is in the zzlextext buffer, add on to it */
void
zzmore ()
{
  zzadd_erase = 2;
}

/* substitute c for the reg. expr last matched and is in the buffer */
void
zzreplchar (c)
     char c;
{
  /* can't allow overwriting null at end of string */
  if (zzbegexpr < &zzlextext[zzbufsize - 1])
    {
      *zzbegexpr = c;
      *(zzbegexpr + 1) = '\0';
    }
  zzendexpr = zzbegexpr;
  zznextpos = zzbegexpr + 1;
}

/* replace the string s for the reg. expr last matched and in the buffer */
void
zzreplstr (s)
     register char *s;
{
  register char *l = &zzlextext[zzbufsize - 1];

  zznextpos = zzbegexpr;
  if (s)
    {
      while ((zznextpos <= l) && (*(zznextpos++) = *(s++)))
	{
	  /* empty */
	}
      /* correct for NULL at end of string */
      zznextpos--;
    }
  if ((zznextpos <= l) && (*(--s) == 0))
    {
      zzbufovfcnt = 0;
    }
  else
    {
      zzbufovfcnt = 1;
    }
  *(zznextpos) = '\0';
  zzendexpr = zznextpos - 1;
}

void
zzgettok ()
{
  register long state, newstate;
  /* last space reserved for the null char */
  register char *lastpos;

skip:
  zzbufovfcnt = 0;
  lastpos = &zzlextext[zzbufsize - 1];
  zznextpos = zzlextext;
  zzbegcol = zzendcol + 1;
more:
  zzbegexpr = zznextpos;
#ifdef ZZINTERACTIVE
  /* interactive version of automaton */
  /* if there is something in zzchar, process it */
  state = newstate = dfa_base[zzauto];
  if (zzcharfull)
    {
      ZZINC;
      ZZCOPY;
      newstate = dfa[state][zzclass];
    }
  if (zzstream_in)
    while (zzalternatives[newstate])
      {
	state = newstate;
	ZZGETC_STREAM;
	ZZINC;
	ZZCOPY;
	newstate = dfa[state][zzclass];
      }
  else if (zzfunc_in)
    while (zzalternatives[newstate])
      {
	state = newstate;
	ZZGETC_STREAM;
	ZZINC;
	ZZCOPY;
	newstate = dfa[state][zzclass];
      }
  /* figure out if last character really part of token */
  if ((state != dfa_base[zzauto]) && (newstate == DfaStates))
    {
      zzcharfull = 1;
      --zznextpos;
    }
  else
    {
      zzcharfull = 0;
      state = newstate;
    }
  *(zznextpos) = '\0';
  /* Able to transition out of start state to some non err state? */
  if (state == dfa_base[zzauto])
    {
      /* make sure doesn't get stuck */
      zzadvance ();
    }
#else
  /* non-interactive version of automaton */
  if (!zzcharfull)
    zzadvance ();
  else
    ZZINC;
  state = dfa_base[zzauto];
  if (zzstream_in)
    while ((newstate = dfa[state][zzclass]) != DfaStates)
      {
	state = newstate;
	ZZCOPY;
	ZZGETC_STREAM;
	ZZINC;
      }
  else if (zzfunc_in)
    while ((newstate = dfa[state][zzclass]) != DfaStates)
      {
	state = newstate;
	ZZCOPY;
	ZZGETC_FUNC;
	ZZINC;
      }
  zzcharfull = 1;
  if (state == dfa_base[zzauto])
    {
      ZZCOPY;
      *zznextpos = '\0';
      /* make sure doesn't get stuck */
      zzadvance ();
    }
  else
    {
      *zznextpos = '\0';
    }
#endif
#ifdef ZZCOL
  zzendcol -= zzcharfull;
#endif
  zzendexpr = zznextpos - 1;
  zzadd_erase = 0;
  (*actions[accepts[state]]) ();
  switch (zzadd_erase)
    {
    case 1:
      goto skip;
    case 2:
      goto more;
    }
}

void
zzadvance ()
{
  if (zzstream_in)
    {
      ZZGETC_STREAM;
      zzcharfull = 1;
      ZZINC;
    }
  if (zzfunc_in)
    {
      ZZGETC_FUNC;
      zzcharfull = 1;
      ZZINC;
    }
  if (!(zzstream_in || zzfunc_in))
    {
      zzerr_in ();
    }
}

#ifndef ZZERRSTD_SUPPLIED
void
zzerrstd (s)
     char *s;
{
  fprintf (stderr,
	   "%s near line %d (zzlextext was '%s')\n",
	   ((s == NULL) ? "Lexical error" : s), zzline, zzlextext);
}
#endif

long
zzerr_in ()
{
  fprintf (stderr, "No input stream or function\n");
  /* return eof to get out gracefully */
  return -1;
}
