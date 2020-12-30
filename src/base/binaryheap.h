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
 * binary heap header
 */

#ifndef _BINARYHEAP_H_
#define _BINARYHEAP_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "config.h"
#include "thread_compat.hpp"

// TODO: which c-code compiled file reaches this header?
#ifdef __cplusplus
extern "C"
{
#endif

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

  typedef enum
  {
    BH_TRY_INSERT_REJECTED,
    BH_TRY_INSERT_ACCEPTED,
    BH_TRY_INSERT_REPLACED
  } BH_TRY_INSERT_RESULT;

  typedef BH_CMP_RESULT (*bh_key_comparator) (const void *left, const void *right, BH_CMP_ARG arg);

  typedef struct binary_heap BINARY_HEAP;
  struct binary_heap
  {
    int max_capacity;
    int elem_size;
    int element_count;
    BH_HEAP_STATE state;
    bh_key_comparator cmp_func;
    BH_CMP_ARG cmp_arg;
    void *members;
    void *swap_buf;
  };

#define BH_ELEMENT(heap, i) (((char *) (heap)->members) + (heap)->elem_size * (i))
#define BH_ROOT(heap)	((heap)->members)

  extern BINARY_HEAP *bh_create (THREAD_ENTRY * thread_p, int max_capacity, int elem_size, bh_key_comparator cmp_func,
				 BH_CMP_ARG cmp_arg);
  extern void bh_destroy (THREAD_ENTRY * thread_p, BINARY_HEAP * heap);

  extern int bh_add (BINARY_HEAP * heap, void *elem);
  extern void bh_build_heap (BINARY_HEAP * heap);

  extern int bh_insert (BINARY_HEAP * heap, void *elem);
  extern BH_TRY_INSERT_RESULT bh_try_insert (BINARY_HEAP * heap, void *elem, void *replaced);

  extern void bh_down_heap (BINARY_HEAP * heap, int index);
  extern bool bh_extract_max (BINARY_HEAP * heap, void *extract_elem);
  extern bool bh_peek_max (BINARY_HEAP * heap, void *peek_elem);

  extern bool bh_is_consistent (BINARY_HEAP * heap);

  extern void bh_to_sorted_array (BINARY_HEAP * heap);

  extern void bh_element_at (BINARY_HEAP * heap, int index, void *elem);
  extern bool bh_is_full (BINARY_HEAP * heap);
#if defined(CUBRID_DEBUG)
  extern bool bh_tests_consistent (BINARY_HEAP * heap);
#endif				/* CUBRID_DEBUG */
#ifdef __cplusplus
}
#endif

#endif				/* _BINARYHEAP_H_ */
