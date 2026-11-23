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
 * btree_sort_radix.hpp - MSD radix sorter of the in-memory run
 *
 * The sorter and the key domains it takes, for btree_sort.c. The key encoding and the engine, for the unit test.
 */

#ifndef _BTREE_SORT_RADIX_HPP_
#define _BTREE_SORT_RADIX_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "btree_sort.h"
#include "object_domain.h"

/* what the stored key is, which says how its radix word is written to order as unsigned bytes */
typedef enum
{
  RADIX_KEY_UNSIGNED = 0,	/* unsigned integer, temporal of one field: as it is */
  RADIX_KEY_DATETIME,		/* the date, then the time, each an unsigned integer: as they are */
  RADIX_KEY_SIGNED,		/* signed integer: the sign bit flipped */
  RADIX_KEY_IEEE,		/* IEEE 754: the sign bit flipped, or every bit of a negative value */
  RADIX_KEY_OID,		/* OID: volume, page and slot in that order */
  RADIX_KEY_MONETARY,		/* currency then an IEEE 754 amount: the amount's word, in IEEE order */
  RADIX_KEY_BIT,		/* BIT: its bytes as they lie, zero past the key, a word at a time over the width */
  RADIX_KEY_VARBIT,		/* VARBIT: its bytes in whole words, zero past them, then a word of their count */
  RADIX_KEY_STRING,		/* CHAR, VARCHAR: the bytes the collation sees, then a count word if shorter_first */
  RADIX_KEY_FIXED_NUMERIC,	/* NUMERIC(p, s): its magnitude right aligned behind a byte for the sign */
  RADIX_KEY_FLOAT_NUMERIC,	/* NUMERIC without a precision: sign, decimal exponent, the magnitude made 40 digits */
  RADIX_KEY_MIDXKEY		/* a MIDXKEY: no word of its own, each column a key of its own format */
} RADIX_KEY_FORMAT;

/* key encoding of an index, worked out of its key domain */
typedef struct radix_key_encoding RADIX_KEY_ENCODING;
struct radix_key_encoding
{
  int word_width;			/* bytes of one radix word: 2, 4 or 8; 0 until resolved */
  int key_length;		/* bytes of the radix key, taken a word at a time over the width */
  int min_key_length;		/* the shortest stored key the domain allows */
  RADIX_KEY_FORMAT format;
  bool is_desc;			/* a descending key: the word is the ascending word's complement */
  bool space_as_zero;		/* a string whose collation treats a space as zero, see resolve_string_collation () */
  bool case_insensitive;	/* a string whose collation treats 'a'..'z' as 'A'..'Z' */
  bool shorter_first;		/* of two values that agree to the shorter one's end, the shorter orders first, so a
				 * word of the byte count ends the radix key */
};

/* the most words of a MIDXKEY's radix key, the words of its columns' radix keys in turn. The sort goes a call
 * deeper for each subarray a word splits, under 500 bytes of stack a call, so 64 words hold the deepest sort near
 * 32KB, little beside a thread stack of megabytes. The columns past them take the comparator */
#define RADIX_MAX_KEY_WORDS 64

/* a column of a MIDXKEY */
typedef struct radix_key_column RADIX_KEY_COLUMN;
struct radix_key_column
{
  RADIX_KEY_ENCODING encoding;	/* as a key of the column's domain alone */
  const TP_DOMAIN *domain;	/* for the walk past the offset table */
};

/* key encoding of a MIDXKEY, worked out of its key domain a column at a time */
typedef struct radix_midxkey RADIX_MIDXKEY;
struct radix_midxkey
{
  int column_count;		/* the key's columns */
  int radix_columns;		/* the leading columns with a radix key, one at least */
  RADIX_KEY_COLUMN columns[RADIX_MAX_KEY_WORDS];
};

/* MSD radix sorter (btree_sort_radix.cpp), for a key domain radix_sort_supports_key () takes */
extern const BTSORT_RUN_SORTER btsort_radix_sorter;

extern bool radix_sort_supports_key (const TP_DOMAIN *key_domain);
extern bool radix_sort_resolve_key (RADIX_KEY_ENCODING *encoding, const TP_DOMAIN *key_domain);
extern size_t radix_sort_stack_bytes (const RADIX_KEY_ENCODING *encoding, int max_records);
extern void radix_sort_run (const RADIX_KEY_ENCODING *encoding, const char *rec_base, BTSORT_RUN_SLOT *slots,
			    BTSORT_RUN_SLOT *slots_spare, int record_count, char *words_area, size_t words_bytes,
			    char *stack_area, size_t stack_bytes);

extern bool radix_sort_resolve_midxkey (RADIX_MIDXKEY *midxkey, RADIX_KEY_ENCODING *encoding,
					const TP_DOMAIN *key_domain);
extern void radix_sort_midxkey_run (const RADIX_MIDXKEY *midxkey, BTSORT_CMP_FUNC *cmp_fn, void *cmp_arg,
				    const char *rec_base, BTSORT_RUN_SLOT *slots, BTSORT_RUN_SLOT *slots_spare,
				    int record_count, char *words_area, size_t words_bytes, char *stack_area,
				    size_t stack_bytes);

#endif /* _BTREE_SORT_RADIX_HPP_ */
