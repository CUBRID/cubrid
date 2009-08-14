/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * query_bitset.c - Bitset operations for sets implemented as machine words
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "query_bitset.h"

#include "memory_alloc.h"

#define NBYTES(n)	((n) * sizeof(BITSET_CARRIER))
#define NELEMS(n)	((n) * _WORDSIZE)

#define bitset_malloc(env, size) malloc(size)
#define bitset_free(ptr)         free_and_init(ptr)

/*
 * The number of one bits in a four-bit nibble.
 */
static const char nbits[] = {
  0,				/* 0000 */
  1,				/* 0001 */
  1,				/* 0010 */
  2,				/* 0011 */
  1,				/* 0100 */
  2,				/* 0101 */
  2,				/* 0110 */
  3,				/* 0111 */
  1,				/* 1000 */
  2,				/* 1001 */
  2,				/* 1010 */
  3,				/* 1011 */
  2,				/* 1100 */
  3,				/* 1101 */
  3,				/* 1110 */
  4,				/* 1111 */
};

static BITSET_CARRIER empty_set_words[NWORDS] = { 0 };
BITSET EMPTY_SET = { NULL, empty_set_words, NWORDS, {{0}}
};

/*
 * set_stats () - Print stats about set usage
 *   return: nothing
 *   set_stats(in):
 *   file(in): the stream on which to print the statistics
 */
void (set_stats) (FILE * file)
{
  fprintf (file, "Set statistics no longer collected\n");
}

/******************************************************************************
 *                                                                            *
 *       		       BITSET FUNCTIONS                               *
 *                                                                            *
 *****************************************************************************/



/*
 * bitset_extend () -
 *   return:
 *   dst(in):
 *   nwords(in):
 */
void
bitset_extend (BITSET * dst, int nwords)
{
  BITSET_CARRIER *words;

  words = (BITSET_CARRIER *) malloc (NBYTES (nwords));
  if (words == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      NBYTES (nwords));
      return;
    }

  memcpy (words, dst->setp, NBYTES (dst->nwords));
  memset (words + dst->nwords, 0, NBYTES (nwords - dst->nwords));
  dst->nwords = nwords;
  if (dst->setp != dst->set.word)
    bitset_free (dst->setp);
  dst->setp = words;
}


/*
 * bitset_assign () -
 *   return:
 *   dst(in):
 *   src(in):
 */
void
bitset_assign (BITSET * dst, const BITSET * src)
{
  if (dst->nwords < src->nwords)
    {
      bitset_extend (dst, src->nwords);
    }

  memcpy (dst->setp, src->setp, NBYTES (src->nwords));
  memset (dst->setp + src->nwords, 0, NBYTES (dst->nwords - src->nwords));
}


/*
 * bitset_add () -
 *   return:
 *   dst(in):
 *   x(in):
 */
void
bitset_add (BITSET * dst, int x)
{
  int n;

  n = _WORD (x);
  if (n >= dst->nwords)
    {
      bitset_extend (dst, n + 1);
    }

  dst->setp[n] |= (1L << _BIT (x));
}


/*
 * bitset_remove () -
 *   return:
 *   dst(in):
 *   x(in):
 */
void
bitset_remove (BITSET * dst, int x)
{
  int n;

  n = _WORD (x);
  if (n < dst->nwords)
    {
      dst->setp[n] &= ~(1L << _BIT (x));
    }
}


/*
 * bitset_union () -
 *   return:
 *   dst(in):
 *   src(in):
 */
void
bitset_union (BITSET * dst, const BITSET * src)
{
  int nwords;

  if (dst->nwords < src->nwords)
    {
      bitset_extend (dst, src->nwords);
    }

  nwords = src->nwords;
  while (nwords)
    {
      nwords -= 1;
      dst->setp[nwords] |= src->setp[nwords];
    }
}


/*
 * bitset_intersect () -
 *   return:
 *   dst(in):
 *   src(in):
 */
void
bitset_intersect (BITSET * dst, const BITSET * src)
{
  int nwords;

  nwords = dst->nwords;
  while (nwords > src->nwords)
    {
      nwords -= 1;
      dst->setp[nwords] = 0;
    }
  while (nwords > 0)
    {
      nwords -= 1;
      dst->setp[nwords] &= src->setp[nwords];
    }
}


/*
 * bitset_difference () -
 *   return:
 *   dst(in):
 *   src(in):
 */
void
bitset_difference (BITSET * dst, const BITSET * src)
{
  int nwords;

  nwords = MIN (dst->nwords, src->nwords);
  while (nwords)
    {
      nwords -= 1;
      dst->setp[nwords] &= ~src->setp[nwords];
    }
}


/*
 * bitset_invert () -
 *   return:
 *   dst(in):
 */
void
bitset_invert (BITSET * dst)
{
  int nwords;

  nwords = dst->nwords;
  while (nwords)
    {
      nwords -= 1;
      dst->setp[nwords] = ~dst->setp[nwords];
    }
}


/*
 * bitset_subset () -
 *   return:
 *   r(in):
 *   s(in):
 */
int
bitset_subset (const BITSET * r, const BITSET * s)
{
  int nwords;

  nwords = s->nwords;
  while (nwords > r->nwords)
    {
      nwords -= 1;
      if (s->setp[nwords])
	return 0;
    }
  while (nwords)
    {
      nwords -= 1;
      if ((r->setp[nwords] & s->setp[nwords]) != s->setp[nwords])
	return 0;
    }

  return 1;
}


/*
 * bitset_intersects () -
 *   return:
 *   r(in):
 *   s(in):
 */
int
bitset_intersects (const BITSET * r, const BITSET * s)
{
  int nwords;

  nwords = MIN (r->nwords, s->nwords);
  while (nwords)
    {
      nwords -= 1;
      if (r->setp[nwords] & s->setp[nwords])
	return 1;
    }

  return 0;
}


/*
 * bitset_is_empty () -
 *   return:
 *   s(in):
 */
int
bitset_is_empty (const BITSET * s)
{
  int nwords;

  nwords = s->nwords;
  while (nwords)
    {
      nwords -= 1;
      if (s->setp[nwords])
	return 0;
    }

  return 1;
}


/*
 * bitset_is_equivalent () -
 *   return:
 *   r(in):
 *   s(in):
 */
int
bitset_is_equivalent (const BITSET * r, const BITSET * s)
{
  int nwords;

  if (r->nwords < s->nwords)
    {
      nwords = s->nwords;
      while (nwords > r->nwords)
	{
	  nwords -= 1;
	  if (s->setp[nwords])
	    return 0;
	}
    }
  else if (r->nwords > s->nwords)
    {
      nwords = r->nwords;
      while (nwords > s->nwords)
	{
	  nwords -= 1;
	  if (r->setp[nwords])
	    return 0;
	}
    }
  else
    {
      nwords = r->nwords;
    }

  while (nwords)
    {
      nwords -= 1;
      if (r->setp[nwords] != s->setp[nwords])
	return 0;
    }

  return 1;
}


/*
 * bitset_cardinality () -
 *   return:
 *   s(in):
 */
int
bitset_cardinality (const BITSET * s)
{
  int nwords, card;

  nwords = s->nwords;
  card = 0;

  while (nwords)
    {
      BITSET_CARRIER word;
      nwords -= 1;
      word = s->setp[nwords];
      while (word)
	{
	  card += nbits[word & 0xf];
	  word >>= 4;
	}
    }

  return card;
}


/*
 * bitset_position () -
 *   return:
 *   s(in):
 *   x(in):
 */
int
bitset_position (const BITSET * s, int x)
{
  int pos;

  if (BITSET_MEMBER (*s, x))
    {
      int i, m;
      BITSET_CARRIER mask, word;

      pos = 0;

      for (i = 0, m = _WORD (x); i < m; i++)
	for (word = s->setp[i]; word; word >>= 4)
	  pos += nbits[word & 0xf];

      mask = (1L << x) - 1;
      for (word = s->setp[m] & mask; word; word >>= 4)
	pos += nbits[word & 0xf];
    }
  else
    pos = -1;

  return pos;
}


/*
 * bitset_iterate () -
 *   return:
 *   s(in):
 *   si(in):
 */
int
bitset_iterate (const BITSET * s, BITSET_ITERATOR * si)
{
  si->set = s;
  si->next = 0;

  return bitset_next_member (si);
}


/*
 * bitset_next_member () -
 *   return:
 *   si(in):
 */
int
bitset_next_member (BITSET_ITERATOR * si)
{
  int nwords;
  BITSET_CARRIER word;
  int current, m;

  current = si->next;

  if (current < 0)
    return -1;

  nwords = si->set->nwords;
  for (m = _WORD (current); m < nwords; current = _WORDSIZE * ++m)
    {
      for (word = si->set->setp[m] >> _BIT (current); word;
	   current++, word >>= 1)
	{
	  if (word & 0x1)
	    {
	      si->next = current + 1;
	      return current;
	    }
	}
    }

  si->next = -1;
  return -1;
}


/*
 * bitset_first_member () -
 *   return:
 *   s(in):
 */
int
bitset_first_member (const BITSET * s)
{
  BITSET_ITERATOR si;
  return bitset_iterate (s, &si);
}


/*
 * bitset_print () -
 *   return:
 *   s(in):
 *   fn(in):
 *   data(in):
 */
void
bitset_print (const BITSET * s, FILE * fp)
{
  if (bitset_is_empty (s))
    {
      (void) fprintf (fp, "empty");
    }
  else
    {
      int i;
      BITSET_ITERATOR si;

      if ((i = bitset_iterate (s, &si)) != -1)
	{
	  (void) fprintf (fp, "%d", i);
	  while ((i = bitset_next_member (&si)) != -1)
	    {
	      (void) fprintf (fp, " %d", i);
	    }
	}
    }
}


/*
 * bitset_init () -
 *   return:
 *   s(in):
 *   env(in):
 */
void
bitset_init (BITSET * s, QO_ENV * env)
{
  s->env = env;
  s->setp = s->set.word;
  s->nwords = NWORDS;
  BITSET_CLEAR (*s);
}


/*
 * bitset_delset () -
 *   return:
 *   s(in):
 */
void
bitset_delset (BITSET * s)
{
  if (s->setp != s->set.word)
    {
      bitset_free (s->setp);
      s->setp = NULL;
    }
}
