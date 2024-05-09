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
 * binaryheap.c - binary heap implementation
 */
#include <stdlib.h>
#include <assert.h>
#include "binaryheap.h"
#include "memory_alloc.h"
#include "error_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define BH_PARENT(i)	((i - 1)/2)
#define BH_LEFT(i)	(2*(i) + 1)
#define BH_RIGHT(i)	(2*(i)+2)
#define BH_ELEMENT_COPY(heap, dest, src) (memcpy (dest, src, (heap)->elem_size))

#define BH_SWAP(heap, left, right)						      \
  do										      \
    {										      \
      BH_ELEMENT_COPY (heap, (heap)->swap_buf, BH_ELEMENT (heap, left));	      \
      BH_ELEMENT_COPY (heap, BH_ELEMENT (heap, left), BH_ELEMENT (heap, right));      \
      BH_ELEMENT_COPY (heap, BH_ELEMENT (heap, right), (heap)->swap_buf);	      \
    }										      \
  while (0)

#define BH_CMP(heap, l, r) \
  (heap->cmp_func (BH_ELEMENT (heap, l), BH_ELEMENT (heap, r), heap->cmp_arg))


static void bh_up_heap (BINARY_HEAP * heap, int index);
static void bh_replace_max (BINARY_HEAP * heap, void *elem);

/*
 * bh_up_heap () - push an element up the heap to the correct position
 * return : void
 * heap (in)  : heap
 * index (in) : index of the element to
 */
static void
bh_up_heap (BINARY_HEAP * heap, int index)
{
  if (index == 0)
    {
      return;
    }

  if (BH_CMP (heap, BH_PARENT (index), index) == BH_LT)
    {
      /* swap element with parent */
      BH_SWAP (heap, index, BH_PARENT (index));
      /* shift parent */
      bh_up_heap (heap, BH_PARENT (index));
    }
}

/*
 * bh_down_heap () - push an element down the heap to its correct position
 * return : void
 * heap (in)  : heap
 * index (in) : element index
 */
void
bh_down_heap (BINARY_HEAP * heap, int index)
{
  int left = BH_LEFT (index);
  int right = BH_RIGHT (index);
  int largest = index;

  if (left <= heap->element_count - 1 && BH_CMP (heap, left, largest) == BH_GT)
    {
      largest = left;
    }

  if (right <= heap->element_count - 1 && BH_CMP (heap, right, largest) == BH_GT)
    {
      largest = right;
    }

  if (largest != index)
    {
      BH_SWAP (heap, index, largest);
      bh_down_heap (heap, largest);
    }
}

/*
 * bh_create () - create an empty heap
 * return : heap or NULL
 * max_capacity (in): maximum capacity of the heap
 * elem_size (in)   : the size of one heap element.
 * cmp_func (in)    : pointer to the comparison function
 * cmp_arg (in)	    : argument to be passed to the comparison function
 *
 *  Note: This implementation considers the heap to be a MAX heap (i.e.: the root of the heap is the "largest" element).
 *	  To use the heap as a MIN heap, callers can use the comparison function to inverse the comparison
 */
BINARY_HEAP *
bh_create (THREAD_ENTRY * thread_p, int max_capacity, int elem_size, bh_key_comparator cmp_func, BH_CMP_ARG cmp_arg)
{
  BINARY_HEAP *heap = NULL;

  heap = (BINARY_HEAP *) db_private_alloc (thread_p, sizeof (BINARY_HEAP));
  if (heap == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (BINARY_HEAP));
      return NULL;
    }

  heap->members = db_private_alloc (thread_p, max_capacity * elem_size);
  if (heap->members == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, max_capacity * elem_size);
      db_private_free (thread_p, heap);
      return NULL;
    }
  heap->swap_buf = db_private_alloc (thread_p, elem_size);
  if (heap->swap_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, elem_size);
      db_private_free (thread_p, heap->members);
      db_private_free (thread_p, heap);
      return NULL;
    }
  memset (heap->members, 0, max_capacity * elem_size);
  heap->max_capacity = max_capacity;
  heap->elem_size = elem_size;
  heap->cmp_func = cmp_func;
  heap->cmp_arg = cmp_arg;
  heap->element_count = 0;
  heap->state = BH_HEAP_CONSISTENT;

  return heap;
}

/*
 * bh_destroy () - destroy a binary heap
 * return : void
 * heap (in) : heap
 */
void
bh_destroy (THREAD_ENTRY * thread_p, BINARY_HEAP * heap)
{
  if (heap != NULL)
    {
      if (heap->members != NULL)
	{
	  db_private_free (thread_p, heap->members);
	}
      if (heap->swap_buf != NULL)
	{
	  db_private_free (thread_p, heap->swap_buf);
	}
      db_private_free (thread_p, heap);
    }
}

/*
 * bh_add () - add an element to the heap without preserving the heap property
 * return : error code if capacity is exceeded or NO_ERROR
 * heap (in) : heap
 * elem (in) : new element
 *
 *  Note: This function is provided as a convenience to speed up heap creation.
 *	  It is more efficient to load about half the heap with unordered elements and call bh_build_heap to restore the
 *	  heap property.
 */
int
bh_add (BINARY_HEAP * heap, void *elem)
{
  assert (heap != NULL);
  assert (elem != NULL);

  heap->state = BH_HEAP_INCONSISTENT;

  if (heap->element_count >= heap->max_capacity)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BINARY_HEAP_OUT_OF_RANGE, 0);
      return ER_BINARY_HEAP_OUT_OF_RANGE;
    }
  BH_ELEMENT_COPY (heap, BH_ELEMENT (heap, heap->element_count), elem);
  heap->element_count++;

  return NO_ERROR;
}

/*
 * bh_insert () - insert an element in the correct position in the heap
 * return : error code or NO_ERROR
 * heap (in) : heap
 * elem (in) : new element
 */
int
bh_insert (BINARY_HEAP * heap, void *elem)
{
  assert (heap != NULL);
  assert (elem != NULL);

  if (heap->element_count != 0)
    {
      assert_release (heap->state == BH_HEAP_CONSISTENT);
    }
  else
    {
      /* This is the first element, set the consistent property */
      heap->state = BH_HEAP_CONSISTENT;
    }

  if (heap->element_count >= heap->max_capacity)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BINARY_HEAP_OUT_OF_RANGE, 0);
      return ER_BINARY_HEAP_OUT_OF_RANGE;
    }

  BH_ELEMENT_COPY (heap, BH_ELEMENT (heap, heap->element_count), elem);
  heap->element_count++;
  bh_up_heap (heap, heap->element_count - 1);

  return NO_ERROR;
}

/*
 * bh_try_insert () - insert an element into the heap if heap hasn't reached the full capacity or if new element is
 *		      smaller than the top element
 * return    : BH_TRY_INSERT_RESULT
 *	       - BH_TRY_INSERT_ACCEPTED if the heap was not full
 *	       - BH_TRY_INSERT_REJECTED if the heap was full and new element was rejected
 *	       - BH_TRY_INSERT_REPLACED if the heap was full and new element replaced old root.
 * heap (in)	  : heap
 * elem (in)	  : new element
 * replaced (out) : value of replaced element (if replaced).
 */
BH_TRY_INSERT_RESULT
bh_try_insert (BINARY_HEAP * heap, void *elem, void *replaced)
{
  assert (heap != NULL);
  assert (elem != NULL);

  if (heap->element_count < heap->max_capacity)
    {
      bh_insert (heap, elem);
      return BH_TRY_INSERT_ACCEPTED;
    }
  /* if root is larger than new element, replace root */
  if (heap->cmp_func (BH_ROOT (heap), elem, heap->cmp_arg) == BH_GT)
    {
      if (replaced != NULL)
	{
	  BH_ELEMENT_COPY (heap, replaced, BH_ROOT (heap));
	}
      bh_replace_max (heap, elem);
      return BH_TRY_INSERT_REPLACED;
    }
  return BH_TRY_INSERT_REJECTED;
}

/*
 * bh_build_heap () - restore the heap property in an unordered heap
 * return : void
 * heap (in) : heap
 */
void
bh_build_heap (BINARY_HEAP * heap)
{
  int i;

  assert (heap != NULL);

  if (heap->state == BH_HEAP_CONSISTENT)
    {
      /* already consistent, nothing to build */
      return;
    }

  for (i = BH_PARENT (heap->element_count - 1); i >= 0; i--)
    {
      bh_down_heap (heap, i);
    }
  heap->state = BH_HEAP_CONSISTENT;
}

/*
 * bh_extract_max () - pop the largest element from a heap
 * return	      : true if heap is not empty and element is extracted, false if heap is empty.
 * heap (in)	      : heap
 * extract_elem (out) : extracted element.
 */
bool
bh_extract_max (BINARY_HEAP * heap, void *extract_elem)
{
  assert (heap != NULL);
  assert (extract_elem != NULL);

  if (heap->element_count <= 0)
    {
      /* empty */
      return false;
    }
  /* extract algorithm is:
   * 1. replace root with last element on the last level.
   * 2. recursive down-swap new root with children until the order is correct.
   */
  /* decrement element count; this also becomes the index of last element in heap */
  heap->element_count--;
  /* Copy root. */
  BH_ELEMENT_COPY (heap, extract_elem, BH_ROOT (heap));
  if (heap->element_count > 0)
    {
      /* replace root with last element. */
      BH_ELEMENT_COPY (heap, BH_ROOT (heap), BH_ELEMENT (heap, heap->element_count));
      /* down-swap until order is correct again */
      bh_down_heap (heap, 0);
    }
  return true;
}

/*
 * bh_replace_max () - replace the largest element from a heap with a new
 *		       element
 * return : void
 * heap (in) : heap
 * elem (in) : new element
 */
void
bh_replace_max (BINARY_HEAP * heap, void *elem)
{
  assert (heap != NULL);
  assert (elem != NULL);

  /* heap_max greater than elem */
  BH_ELEMENT_COPY (heap, BH_ROOT (heap), elem);
  bh_down_heap (heap, 0);
}

/*
 * bh_peek_max () - peek the value of the largest element in the heap
 * return	   : true if heap is not empty and element is extracted, false if heap is empty.
 * heap (in)	   : heap
 * peek_elem (out) : peeked value of largest element.
 */
bool
bh_peek_max (BINARY_HEAP * heap, void *peek_elem)
{
  assert (heap != NULL);
  assert (peek_elem != NULL);

  if (heap->element_count == 0)
    {
      return false;
    }
  BH_ELEMENT_COPY (heap, peek_elem, BH_ROOT (heap));
  return true;
}

/*
 * bh_to_sorted_array () - transform a binary heap into a sorted array
 * return    : void
 * heap (in) : heap
 *
 *  Note: the array is sorted smallest to largest (smallest element on the first position)
 */
void
bh_to_sorted_array (BINARY_HEAP * heap)
{
  int element_count;

  assert (heap != NULL);

  element_count = heap->element_count;
  /* while has elements, extract max */
  while (heap->element_count > 1)
    {
      BH_SWAP (heap, 0, heap->element_count - 1);
      heap->element_count--;
      bh_down_heap (heap, 0);
    }
  /* no longer consistent */
  heap->state = BH_SORTED_ARRAY;

  /* reset element count */
  heap->element_count = element_count;
}

#if defined(CUBRID_DEBUG)
/*
 * bh_tests_consistent () - test if the elements stored in the heap
 *			    have the heap property
 * return : true if the heap is consistent, false otherwise
 * heap (in) : heap
 */
int
bh_tests_consistent (BINARY_HEAP * heap)
{
  int i;

  assert (heap != NULL);

  if (heap->element_count <= 1)
    {
      /* at most one element, it is consistent */
      heap->state = BH_HEAP_CONSISTENT;
      return true;
    }

  /* test heap property: CHILD <= PARENT */
  for (i = 1; i < heap->element_count; i++)
    {
      if (BH_CMP (heap, BH_PARENT (i), i) == BH_LT)
	{
	  heap->state = BH_HEAP_INCONSISTENT;
	  return false;
	}
    }
  heap->state = BH_HEAP_CONSISTENT;
  return true;
}
#endif

/*
 * bh_element_at () - get element at specified index from the array
 * return : element
 * heap (in)  : heap
 * index (in) : index
 * elem (out) : element at index.
 */
void
bh_element_at (BINARY_HEAP * heap, int index, void *elem)
{
  assert (heap != NULL);
  assert (index < heap->element_count);
  assert (elem != NULL);

  BH_ELEMENT_COPY (heap, elem, BH_ELEMENT (heap, index));
}

/*
 * bh_is_consistent () - return true if heap is in consistent state
 * return : true or false
 * heap (in) : heap
 */
bool
bh_is_consistent (BINARY_HEAP * heap)
{
  assert (heap != NULL);
  return (heap->state == BH_HEAP_CONSISTENT);
}

/*
 * bh_is_full () - test if heap holds maximum capacity elements
 * return : true if heap is at maximum capacity
 * heap (in) : heap
 */
bool
bh_is_full (BINARY_HEAP * heap)
{
  assert (heap != NULL);
  return (heap->element_count >= heap->max_capacity);
}
