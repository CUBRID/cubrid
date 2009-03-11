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

#include "connection_list_sr.h"
#include "connection_error.h"

#ifdef TRACE_LIST
static int css_check_list (CSS_LIST * ptr);
#endif /* TRACE_LIST */
static int compare_data (void *data, void *compare);

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
  CSS_ASSERT ((list->back == prev) && (list->count == i));

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
  CSS_ASSERT (css_check_list (list));
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

  CSS_ASSERT (list->free_count == 0);
  CSS_ASSERT (list->count == 0);

  PRINT_FINALIZE_LIST (list);

  return NO_ERROR;
}

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
  CSS_ASSERT (css_check_list (list));
#endif /* TRACE_LIST */

  list->count++;
  e->next = NULL;
  *list->back = e;
  list->back = &e->next;

#ifdef TRACE_LIST
  CSS_ASSERT (css_check_list (list));
#endif /* TRACE_LIST */

  return NO_ERROR;
}

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
  CSS_ASSERT (css_check_list (list));
#endif /* TRACE_LIST */

  list->count++;
  e->next = list->front;
  list->front = e;
  if (e->next == NULL)
    {
      list->back = &e->next;
    }

#ifdef TRACE_LIST
  CSS_ASSERT (css_check_list (list));
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
	  CSS_CHECK_RETURN_ERROR (ER_CSS_INVALID_RETURN_VALUE,
				  ER_CSS_INVALID_RETURN_VALUE);
	}
    }

  return NO_ERROR;
}
