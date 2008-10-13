/* err.h
 *
 * Standard error handling mechanism
 *
 * Terence Parr, Hank Dietz and Will Cohen: 1989, 1990, 1991
 * Purdue University Electrical Engineering
 * ANTLR Version 1.00
 *
 * $Revision: 1.5 $
 */

#include "mtdef.h"

extern zzconsume2 ();
extern zzconsume ();

#ifndef ZZRESYNCHSUPPLIED
void
zzresynch (wd, mask)
     unsigned long *wd, mask;
{
  if (LA (1) == zzEOF_TOKEN)
    return;

  /* We must consume at least one token.  Otherwise, error repair can loop forever. */
  do
    {
      zzCONSUME;
    }
  while (!(wd[LA (1)] & mask) && LA (1) != zzEOF_TOKEN);
}
#endif

char *
zzedecode (p)
     unsigned long *p;
{

  /* maximum of 32 bits/unsigned long and must be 8 bits/byte */
  PRIVATE_TLS unsigned long bitmask[] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000
  };
  register unsigned long *endp = &(p[zzSET_SIZE]);
  register unsigned long e = 0;
  PRIVATE_TLS char strbuf[4096];

  strcpy (strbuf, "{");
  do
    {
      register unsigned long t = 0;
      register unsigned long *b = &(bitmask[0]);
      if (p)
	t = *p;
      do
	{
	  if (t & *b)
	    {
	      strcat (strbuf, " ");
	      strcat (strbuf, zztokens[e]);
	    }
	  e++;
	}
      while (++b < &(bitmask[32]));
    }
  while (++p < endp);
  strcat (strbuf, " }");
  return strbuf;
}


#ifndef ZZSYNSUPPLIED

/* standard error reporting function */
void
zzsyn (text, tok, egroup, eset, etok)
     const char *text, *egroup;
     long tok, etok;
     unsigned long *eset;
{

  fprintf (stderr, "line %d: syntax error at \"%s\"",
#ifdef LL_K
	   zzlineLA[0],
#else
	   zzline,
#endif
	   (tok == zzEOF_TOKEN) ? "EOF" : text);
  fprintf (stderr, " missing ");
  if (!etok)
    fprintf (stderr, "%s", zzedecode (eset));
  else
    fprintf (stderr, "%s", zztokens[etok]);
  if (strlen (egroup) > 0)
    fprintf (stderr, " in %s", egroup);
  fprintf (stderr, "\n");
}

#endif
