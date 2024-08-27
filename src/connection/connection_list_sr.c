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
 * connection_list_sr.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#if !defined(WINDOWS)
#include <netinet/in.h>
#include <poll.h>
#endif /* !WINDOWS */
#include <string.h>
#include <malloc.h>
#include <memory.h>
#include <errno.h>
#include <assert.h>

#include "connection_list_sr.h"
#include "connection_error.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#ifdef TRACE_LIST
static int css_check_list (CSS_LIST * ptr);
#endif /* TRACE_LIST */
#if defined (ENABLE_UNUSED_FUNCTION)
static int compare_data (void *data, void *compare);
#endif

#ifdef TRACE_LIST
/*
 * css_check_list() - check list contents.
 *   return: 1
 *   ptr(in): list
 *
 */
static int
css_check_list (CSS_LIST * list)
{
  int i = 0;
  CSS_LIST_ENTRY *e;
  CSS_LIST_ENTRY **prev = NULL;

  e = list->front;
  prev = &list->front;
  while (e)
    {
      i++;
      prev = &e->next;
      e = e->next;
    }
  assert ((list->back == prev) && (list->count == i));

  return 1;
}
#endif

/*
 * css_initialize_list() - list initialization
 *   return: 0 if success, or error code
 *   ptr(in/out): list
 *   free_count(in): count of entry to allocate previously
 */
int
css_initialize_list (CSS_LIST * list, int free_count)
{
  int i;
  CSS_LIST_ENTRY *e;

  list->back = &list->front;
  list->front = NULL;
  list->count = 0;

#ifdef TRACE_LIST
  assert (css_check_list (list));
#endif /* TRACE_LIST */

  list->free_list = NULL;
  list->free_count = 0;
  for (i = 0; i < free_count; i++)
    {
      e = (CSS_LIST_ENTRY *) malloc (sizeof (CSS_LIST_ENTRY));
      if (e == NULL)
	{
	  CSS_CHECK_RETURN_ERROR (ER_CSS_ALLOC, ER_CSS_ALLOC);
	}
      e->next = list->free_list;
      list->free_list = e;
      list->free_count++;
    }

  PRINT_INIT_LIST (list);

  return NO_ERROR;
}

/*
 * css_finalize_list() - destroy the list
 *   return: 0 if success, or error code
 *   ptr(in/out): list
 */
int
css_finalize_list (CSS_LIST * list)
{
  CSS_LIST_ENTRY *e;

  while (list->free_list != NULL)
    {
      e = list->free_list;
      list->free_list = e->next;
      free_and_init (e);
      list->free_count--;
    }

  assert (list->free_count == 0);
  assert (list->count == 0);

  PRINT_FINALIZE_LIST (list);

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_list_isempty() - check if list is empty
 *   return: true if it is empty, or false
 *   ptr(in): list
 */
bool
css_list_isempty (CSS_LIST * list)
{
  return (list->count == 0);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_add_list() - add an element to last of the list
 *   return: 0 if success, or error code
 *   ptr(in/out): list
 *   item: item to add
 */
int
css_add_list (CSS_LIST * list, void *item)
{
  CSS_LIST_ENTRY *e;

  e = list->free_list;
  if (e != NULL)
    {
      list->free_list = e->next;
      list->free_count--;
    }
  else
    {
      e = (CSS_LIST_ENTRY *) malloc (sizeof (*e));
      if (e == NULL)
	{
	  CSS_CHECK_RETURN_ERROR (ER_CSS_ALLOC, ER_CSS_ALLOC);
	}
    }

  e->data = item;

#ifdef TRACE_LIST
  assert (css_check_list (list));
#endif /* TRACE_LIST */

  list->count++;
  e->next = NULL;
  *list->back = e;
  list->back = &e->next;

#ifdef TRACE_LIST
  assert (css_check_list (list));
#endif /* TRACE_LIST */

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_add_list_to_head() - add an element to head of the list
 *   return: 0 if success, or error code
 *   ptr(in/out): list
 *   item: item to add
 */
int
css_add_list_to_head (CSS_LIST * list, void *item)
{
  CSS_LIST_ENTRY *e;

  e = list->free_list;
  if (e != NULL)
    {
      list->free_list = e->next;
      list->free_count--;
    }
  else
    {
      e = (CSS_LIST_ENTRY *) malloc (sizeof (*e));
      if (e == NULL)
	{
	  CSS_CHECK_RETURN_ERROR (ER_CSS_ALLOC, ER_CSS_ALLOC);
	}
    }

  e->data = item;

#ifdef TRACE_LIST
  assert (css_check_list (list));
#endif /* TRACE_LIST */

  list->count++;
  e->next = list->front;
  list->front = e;
  if (e->next == NULL)
    {
      list->back = &e->next;
    }

#ifdef TRACE_LIST
  assert (css_check_list (list));
#endif /* TRACE_LIST */

  return NO_ERROR;
}

/*
 * compare_data() - compare function for list traverse
 *   return: status of traverse
 *   data(in): data for compare
 *   compare(in): data for compare
 */
static int
compare_data (void *data, void *compare)
{
  return ((data == compare) ? TRAV_STOP_DELETE : TRAV_CONT);
}

/*
 * css_remove_list() - remove the first entry matching the value of item
 *   return: 0 if success, or error code
 *   ptr(in): list
 *   item: item to remove
 */
int
css_remove_list (CSS_LIST * list, void *item)
{
  return css_traverse_list (list, compare_data, item);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_remove_list_from_head() - remove the first entry of the list
 *   return: removed item
 *   ptr(in/out): list
 */
void *
css_remove_list_from_head (CSS_LIST * list)
{
  CSS_LIST_ENTRY *e;
  void *data = NULL;

  e = list->front;

  if (e)
    {
      list->front = e->next;
      if (list->front == NULL)
	{
	  list->back = &list->front;
	}
      list->count--;

      data = e->data;

      e->next = list->free_list;
      list->free_list = e;
      list->free_count++;
    }

  return data;
}

/*
 * css_traverse_list() - traverse the list and apply the func for each entry
 *   return: 0 if success, or error code
 *   ptr(in/out): list
 *   func(in): compare function
 *   arg(in): argument
 */
int
css_traverse_list (CSS_LIST * list, int (*func) (void *, void *), void *arg)
{
  CSS_LIST_ENTRY **prev, *e;

  prev = &list->front;
  e = list->front;

  while (e)
    {
      switch (func (e->data, arg))
	{
	case TRAV_CONT:
	  /* continue applying func */
	  prev = &e->next;
	  e = e->next;
	  break;

	case TRAV_STOP:
	  /* stop applying func */
	  return 0;

	case TRAV_STOP_DELETE:
	  /* stop applying func. delete and free current entry */
	  *prev = e->next;
	  if (*prev == NULL)
	    {
	      list->back = prev;
	    }
	  e->next = list->free_list;
	  list->free_list = e;
	  list->free_count++;
	  list->count--;
	  return 0;

	case TRAV_CONT_DELETE:
	  /* continue applying func. delete and free current entry */
	  *prev = e->next;
	  if (*prev == NULL)
	    {
	      list->back = prev;
	    }
	  e->next = list->free_list;
	  list->free_list = e;
	  list->free_count++;
	  e = *prev;
	  list->count--;
	  break;

	default:
	  /* function returns invalid value */
	  CSS_CHECK_RETURN_ERROR (ER_CSS_INVALID_RETURN_VALUE, ER_CSS_INVALID_RETURN_VALUE);
	}
    }

  return NO_ERROR;
}
