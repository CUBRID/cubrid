/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * bitset.h - Extendible bitset implementation
 */

#ifndef _BITSET_H_
#define _BITSET_H_

#ident "$Id$"

#include <stdio.h>
#include <string.h>

#include "optimizer.h"

/*
 * The maximum number of elements permitted in a set.
 */

#define NELEMENTS 64

#define _LOG2_WORDSIZE	5
#define _WORDSIZE	32	/* Number of bits in BITSET_CARRIER */
#define _MASK		((1L << _LOG2_WORDSIZE) - 1)
#define _WORD(x)	((x) >> _LOG2_WORDSIZE)
#define _BIT(x)		((x) & _MASK)

#define NWORDS (((NELEMENTS + (_WORDSIZE-1)) & ~(_WORDSIZE-1)) \
		>> _LOG2_WORDSIZE)

typedef unsigned int BITSET_CARRIER;
typedef struct bitset_iterator BITSET_ITERATOR;

struct bitset
{
  QO_ENV *env;
  int nwords;
  BITSET_CARRIER *setp;
  struct
  {
    BITSET_CARRIER word[NWORDS];
  } set;

};

struct bitset_iterator
{
  const BITSET *set;
  int next;

};

#define BITSET_CLEAR(s)		memset((char *)(s).setp, 0, \
				  (s).nwords * sizeof(BITSET_CARRIER))
#define BITSET_MEMBER(s, x)	((_WORD(x) < (s).nwords) \
				  && ((s).setp[_WORD(x)]  & (1L << _BIT(x))))
#define BITPATTERN(s)		((s).setp[0])

/*
 * Use these macros when you have to actually move a bitset from one
 * region to another; it will maintain the proper self-relative pointer
 * if necessary.  DON'T do DELSET() on the old set after using this
 * macro!
 */
#define BITSET_MOVE(dst, src) \
    do { \
	(dst) = (src); \
	if ((src).setp == (src).set.word) \
	    (dst).setp = (dst).set.word; \
    } while(0)

extern BITSET EMPTY_SET;

extern void set_stats (FILE *);
extern void bitset_extend (BITSET * dst, int nwords);
extern void bitset_assign (BITSET *, const BITSET *);
extern void bitset_add (BITSET *, int);
extern void bitset_remove (BITSET *, int);
extern void bitset_union (BITSET *, const BITSET *);
extern void bitset_intersect (BITSET *, const BITSET *);
extern void bitset_difference (BITSET *, const BITSET *);
extern void bitset_invert (BITSET *);
extern int bitset_subset (const BITSET *, const BITSET *);
extern int bitset_intersects (const BITSET *, const BITSET *);
extern int bitset_is_empty (const BITSET *);
extern int bitset_is_equivalent (const BITSET *, const BITSET *);
extern int bitset_cardinality (const BITSET *);
extern int bitset_position (const BITSET *, int);
extern int bitset_iterate (const BITSET *, BITSET_ITERATOR *);
extern int bitset_next_member (BITSET_ITERATOR *);
extern int bitset_first_member (const BITSET *);
extern void bitset_print (const BITSET *, int (*)(void *, char *, int),
			  void *);
extern void bitset_init (BITSET *, QO_ENV *);
extern void bitset_delset (BITSET *);

#endif /* _BITSET_H_ */
