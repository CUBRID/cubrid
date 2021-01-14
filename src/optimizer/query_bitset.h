/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * query_bitset.h - Extendible bitset implementation
 */

#ifndef _QUERY_BITSET_H_
#define _QUERY_BITSET_H_

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
  BITSET_CARRIER *setp;
  int nwords;
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

#if defined (CUBRID_DEBUG)
extern void set_stats (FILE * fp);
#endif
extern void bitset_extend (BITSET * dst, int nwords);
extern void bitset_assign (BITSET *, const BITSET *);
extern void bitset_add (BITSET *, int);
extern void bitset_remove (BITSET *, int);
extern void bitset_union (BITSET *, const BITSET *);
extern void bitset_intersect (BITSET *, const BITSET *);
extern void bitset_difference (BITSET *, const BITSET *);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void bitset_invert (BITSET *);
extern int bitset_position (const BITSET *, int);
#endif
extern int bitset_subset (const BITSET *, const BITSET *);
extern int bitset_intersects (const BITSET *, const BITSET *);
extern int bitset_is_empty (const BITSET *);
extern int bitset_is_equivalent (const BITSET *, const BITSET *);
extern int bitset_cardinality (const BITSET *);
extern int bitset_iterate (const BITSET *, BITSET_ITERATOR *);
extern int bitset_next_member (BITSET_ITERATOR *);
extern int bitset_first_member (const BITSET *);
extern void bitset_print (const BITSET *, FILE * fp);
extern void bitset_init (BITSET *, QO_ENV *);
extern void bitset_delset (BITSET *);

#endif /* _QUERY_BITSET_H_ */
