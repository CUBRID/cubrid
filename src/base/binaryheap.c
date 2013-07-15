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
 * binaryheap.c - binary heap implementation
 */
#include <stdlib.h>
#include <assert.h>
#include "binaryheap.h"
#include "memory_alloc.h"
#include "error_manager.h"

#define BH_PARENT(i)	((i - 1)/2)
#define BH_LEFT(i)	(2*(i) + 1)
#define BH_RIGHT(i)	(2*(i)+2)
#define BH_ELEMENT(heap, i) ((heap)->members[(i)])

#define BH_SWAP(heap, left, right)			\
  do							\
  {							\
    BH_ELEM *tmp = BH_ELEMENT(heap, left);		\
    BH_ELEMENT (heap, left) = BH_ELEMENT (heap, right);	\
    BH_ELEMENT (heap, right) = tmp;			\
  } while (0)

#define BH_CMP(heap, l, r) \
  (heap->cmp_func (BH_ELEMENT (heap, l), BH_ELEMENT (heap, r), heap->cmp_arg))


static void bh_up_heap (BINARY_HEAP * heap, int index);

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

  if (left <= heap->element_count - 1
      && BH_CMP (heap, left, largest) == BH_GT)
    {
      largest = left;
    }

  if (right <= heap->element_count - 1
      && BH_CMP (heap, right, largest) == BH_GT)
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
 * cmp_func (in)    : pointer to the comparison function
 * cmp_arg (in)	    : argument to be passed to the comparison function
 *
 *  Note: this implementation considers the heap to be a MAX heap (i.e.: the
 *    root of the heap is the "largest" element). To use the heap as a MIN
 *    heap, callers can use the comparison function to inverse the comparison
 */
BINARY_HEAP *
bh_create (THREAD_ENTRY * thread_p, int max_capacity,
	   bh_key_comparator cmp_func, BH_CMP_ARG cmp_arg)
{
  BINARY_HEAP *heap = NULL;

  heap = (BINARY_HEAP *) db_private_alloc (thread_p, sizeof (BINARY_HEAP));
  if (heap == NULL)
    {
      return NULL;
    }

  heap->members =
    (BH_ELEM *) db_private_alloc (thread_p, max_capacity * sizeof (BH_ELEM));
  if (heap->members == NULL)
    {
      db_private_free (thread_p, heap);
      return NULL;
    }
  memset (heap->members, 0, max_capacity * sizeof (BH_ELEM));
  heap->max_capacity = max_capacity;
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
      db_private_free (thread_p, heap);
    }
}

/*
 * bh_add () - add an element to the heap without preserving the heap property
 * return : error code if capacity is exceeded or NO_ERROR
 * heap (in) : heap
 * elem (in) : new element
 *
 *  Note: This function is provided as a convenience to speed up heap creation
 *    It is more efficient to load about half the heap with unordered elements
 *    and call bh_build_heap to restore the heap property
 */
int
bh_add (BINARY_HEAP * heap, BH_ELEM elem)
{
  heap->state = BH_HEAP_INCONSISTENT;

  if (heap->element_count >= heap->max_capacity)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BINARY_HEAP_OUT_OF_RANGE,
	      0);
      return ER_BINARY_HEAP_OUT_OF_RANGE;
    }
  heap->members[heap->element_count] = elem;
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
bh_insert (BINARY_HEAP * heap, BH_ELEM elem)
{
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BINARY_HEAP_OUT_OF_RANGE,
	      0);
      return ER_BINARY_HEAP_OUT_OF_RANGE;
    }

  heap->members[heap->element_count] = elem;
  heap->element_count++;
  bh_up_heap (heap, heap->element_count - 1);

  return NO_ERROR;
}

/*
 * bh_try_insert () - insert an element into the heap if heap hasn't reached
 *		      the full capacity or if new element is smaller than the
 *		      top element
 * return : old root element if it was replaced
 * heap (in) : heap
 * elem (in) : new element
 */
BH_ELEM
bh_try_insert (BINARY_HEAP * heap, BH_ELEM elem)
{
  if (heap->element_count < heap->max_capacity)
    {
      bh_insert (heap, elem);
      return NULL;
    }
  /* if root is larger than new element, replace root */
  if (heap->cmp_func (heap->members[0], elem, heap->cmp_arg) == DB_GT)
    {
      return bh_replace_max (heap, elem);
    }
  return elem;
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
 * bh_extract_max () - remove the largest element from a heap
 * return : removed element
 * heap (in) : heap
 */
BH_ELEM
bh_extract_max (BINARY_HEAP * heap)
{
  int idx = heap->element_count - 1;
  if (idx <= 0)
    {
      heap->element_count = 0;
      return heap->members[0];
    }
  BH_SWAP (heap, 0, idx);
  heap->element_count--;
  bh_down_heap (heap, 0);

  return BH_ELEMENT (heap, idx);
}

/*
 * bh_replace_max () - replace the largest element from a heap with a new
 *		       element
 * return : replaced element
 * heap (in) : heap
 * elem (in) : new element
 */
BH_ELEM
bh_replace_max (BINARY_HEAP * heap, BH_ELEM elem)
{
  BH_ELEM replaced = NULL;
  /* heap_max greater than elem */
  replaced = heap->members[0];
  heap->members[0] = elem;
  bh_down_heap (heap, 0);
  return replaced;
}

/*
 * bh_peek_max () - peek the value of the largest element in the heap
 * return : largest element
 * heap (in) : heap
 *
 * Note: The returned value should never be changed outside of the heap
 *  API, otherwise, the heap property might be invalidated
 */
BH_ELEM
bh_peek_max (BINARY_HEAP * heap)
{
  if (heap->element_count == 0)
    {
      return NULL;
    }
  return BH_ELEMENT (heap, 0);
}

/*
 * bh_to_sorted_array () - transform a binary heap into a sorted array
 * return : void
 * heap (in) : heap
 *
 *  Note: the array is sorted smallest to largest (smallest element on the
 *  first position)
 */
void
bh_to_sorted_array (BINARY_HEAP * heap)
{
  int element_count = heap->element_count;
  /* while has elements, extract max */
  while (heap->element_count > 0)
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
  if (heap->element_count <= 1)
    {
      /* at most one element, it is consistent */
      heap->state = BH_HEAP_CONSISTENT;
      return true;
    }

  /* test heap property: CHILD <= PARENT */
  for (i = 1; i < heap->element_count; i++)
    {
      if (BH_CMP (heap, BH_PARENT (i), i) == DB_LT)
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
 */
BH_ELEM
bh_element_at (BINARY_HEAP * heap, int index)
{
  assert_release (index < heap->element_count);
  return BH_ELEMENT (heap, index);
}

/*
 * bh_is_consistent () - return true if heap is in consistent state
 * return : true or false
 * heap (in) : heap
 */
bool
bh_is_consistent (BINARY_HEAP * heap)
{
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
  return (heap->element_count >= heap->max_capacity);
}
