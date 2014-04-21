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
 * binary heap header
 */

#ifndef _BINARYHEAP_H_
#define _BINARYHEAP_H_

#include "config.h"
#include "thread.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef void *BH_ELEM;
  typedef void *BH_CMP_ARG;

  typedef enum
  {
    BH_CMP_ERROR = -2,
    BH_LT = -1,
    BH_EQ = 0,
    BH_GT = 1
  } BH_CMP_RESULT;

  typedef enum
  {
    BH_HEAP_INCONSISTENT,
    BH_HEAP_CONSISTENT,
    BH_SORTED_ARRAY
  } BH_HEAP_STATE;

  typedef BH_CMP_RESULT (*bh_key_comparator) (const BH_ELEM left,
					      const BH_ELEM right,
					      BH_CMP_ARG arg);

  typedef struct binary_heap BINARY_HEAP;
  struct binary_heap
  {
    int max_capacity;
    int element_count;
    BH_HEAP_STATE state;
    bh_key_comparator cmp_func;
    BH_CMP_ARG cmp_arg;
    BH_ELEM *members;
  };

  extern BINARY_HEAP *bh_create (THREAD_ENTRY * thread_p, int max_capacity,
				 bh_key_comparator cmp_func,
				 BH_CMP_ARG cmp_arg);
  extern void bh_destroy (THREAD_ENTRY * thread_p, BINARY_HEAP * heap);

  extern int bh_add (BINARY_HEAP * heap, BH_ELEM elem);
  extern void bh_build_heap (BINARY_HEAP * heap);

  extern int bh_insert (BINARY_HEAP * heap, BH_ELEM elem);
  extern BH_ELEM bh_try_insert (BINARY_HEAP * heap, BH_ELEM elem);

  extern void bh_down_heap (BINARY_HEAP * heap, int index);
  extern BH_ELEM bh_extract_max (BINARY_HEAP * heap);
  extern BH_ELEM bh_replace_max (BINARY_HEAP * heap, BH_ELEM elem);
  extern BH_ELEM bh_peek_max (BINARY_HEAP * heap);

  extern bool bh_is_consistent (BINARY_HEAP * heap);

  extern void bh_to_sorted_array (BINARY_HEAP * heap);

  extern BH_ELEM bh_element_at (BINARY_HEAP * heap, int index);
  extern bool bh_is_full (BINARY_HEAP * heap);
#if defined(CUBRID_DEBUG)
  extern bool bh_tests_consistent (BINARY_HEAP * heap);
#endif				/* CUBRID_DEBUG */
#ifdef __cplusplus
}
#endif

#endif				/* _BINARYHEAP_H_ */
