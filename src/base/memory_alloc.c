/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * memory_alloc.c - Memory allocation module
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "misc_string.h"
#include "dbtype.h"
#include "memory_alloc.h"
#include "util_func.h"
#include "error_manager.h"
#include "intl_support.h"
#include "thread_impl.h"
#include "customheaps.h"
#if !defined (SERVER_MODE)
#include "quick_fit.h"
#endif /* SERVER_MODE */

#define DEFAULT_OBSTACK_CHUNK_SIZE      32768 /* 1024 x 32 */

#if !defined (SERVER_MODE)
extern unsigned int db_on_server;
unsigned int private_heap_id = 0;
#endif /* SERVER_MODE */

/*
 * ansisql_strcmp - String comparison according to ANSI SQL
 *   return: an integer value which is less than zero
 *           if s is lexicographically less than t,
 *           equal to zero if s is equal to t,
 *           and greater than zero if s is greater than zero.
 *   s(in): first string to be compared
 *   t(in): second string to be compared
 *
 * Note: The contents of the null-terminated string s are compared with
 *       the contents of the null-terminated string t, using the ANSI
 *       SQL semantics. That is, if the lengths of the strings are not
 *       the same, the shorter string is considered to be extended
 *       with the blanks on the right, so that both strings have the
 *       same length.
 */
int
ansisql_strcmp (const char *s, const char *t)
{
  for (; *s == *t; s++, t++)
    if (*s == '\0')
      return 0;

  if (*s == '\0')
    {
      while (*t != '\0')
	if (*t++ != ' ')
	  return -1;
      return 0;
    }
  else if (*t == '\0')
    {
      while (*s != '\0')
	if (*s++ != ' ')
	  return 1;
      return 0;
    }
  else
    return (*(unsigned const char *) s < *(unsigned const char *) t) ? -1 : 1;
}

/*
 * ansisql_strcasecmp - Case-insensitive string comparison according to ANSI SQL
 *   return: an integer value which is less than zero
 *           if s is lexicographically less than t,
 *           equal to zero if s is equal to t,
 *           and greater than zero if s is greater than zero.
 *   s(in): first string to be compared
 *   t(in): second string to be compared
 *
 * Note: The contents of the null-terminated string s are compared with
 *       the contents of the null-terminated string t, using the ANSI
 *       SQL semantics. That is, if the lengths of the strings are not
 *       the same, the shorter string is considered to be extended
 *       with the blanks on the right, so that both strings have the
 *       same length.
 */
int
ansisql_strcasecmp (const char *s, const char *t)
{
  size_t s_length, t_length, min_length;
  int cmp_val;

  s_length = strlen (s);
  t_length = strlen (t);
  min_length = s_length < t_length ? s_length : t_length;
  cmp_val = intl_mbs_ncasecmp (s, t, min_length);
  /* If not equal for shorter length, return */
  if (cmp_val)
    return cmp_val;
  /* If equal and same size, return */
  if (s_length == t_length)
    return 0;
  /* If equal for shorter length and not same size, look for trailing blanks */
  s += min_length;
  t += min_length;

  if (*s == '\0')
    {
      while (*t != '\0')
	if (*t++ != ' ')
	  return -1;
      return 0;
    }
  else
    {
      while (*s != '\0')
	if (*s++ != ' ')
	  return 1;
      return 0;
    }
}

/*
 * db_alignment () -
 *   return:
 *   n(in):
 */
int
db_alignment (int n)
{
  return (n >= (int) sizeof (double)) ? (int) sizeof (double) :
    (n >= (int) sizeof (void *))? (int) sizeof (void *) :
    (n >= (int) sizeof (int)) ? (int) sizeof (int) :
    (n >= (int) sizeof (short)) ? (int) sizeof (short) : 1;
}

/*
 * db_align_to () - Return the least multiple of 'alignment' that is greater
 * than or equal to 'n'.
 *   return:
 *   n(in):
 *   alignment(in):
 */
uintptr_t
db_align_to (uintptr_t n, int alignment)
{
  /*
   * Return the least multiple of 'alignment' that is greater than or
   * equal to 'n'.  'alignment' must be a power of 2.
   */
  return (uintptr_t) (((uintptr_t) n +
		       ((uintptr_t) alignment -
			1)) & ~((uintptr_t) alignment - 1));
}

/*
 * db_create_ostk_heap () - create an obstack heap
 *   return: memory heap identifier
 *   chunk_size(in):
 */
unsigned int
db_create_ostk_heap (int chunk_size)
{
  return hl_register_ostk_heap (chunk_size);
}

/*
 * db_destroy_ostk_heap () - destroy an obstack heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_ostk_heap (unsigned int heap_id)
{
  hl_unregister_ostk_heap (heap_id);
}

/*
 * db_ostk_alloc () - call allocation function for the obstack heap
 *   return: allocated memory pointer
 *   heap_id(in): memory heap identifier
 *   size(in): size to allocate
 */
void *
db_ostk_alloc (unsigned int heap_id, size_t size)
{
  void *ptr = NULL;
  if (heap_id && size > 0)
    {
      ptr = hl_ostk_alloc (heap_id, size);
    }
  return ptr;
}

/*
 * db_ostk_free () - call free function for the ostk heap
 *   return:
 *   heap_id(in): memory heap identifier
 *   ptr(in): memory pointer to free
 */
void
db_ostk_free (unsigned int heap_id, void *ptr)
{
  if (heap_id && ptr)
    {
      hl_ostk_free (heap_id, ptr);
    }
}

/*
 * db_create_private_heap () - create a thread specific heap
 *   return: memory heap identifier
 */
unsigned int
db_create_private_heap (void)
{
  unsigned int heap_id = 0;
#if defined (SERVER_MODE)
  heap_id = hl_register_ostk_heap (DEFAULT_OBSTACK_CHUNK_SIZE);
#else /* SERVER_MODE */
  if (db_on_server)
    {
      heap_id = hl_register_ostk_heap (DEFAULT_OBSTACK_CHUNK_SIZE);
    }
#endif /* SERVER_MODE */
  return heap_id;
}

/*
 * db_clear_private_heap () - clear a thread specific heap
 *   return:
 *   heap_id(in): memory heap identifier to clear
 */
void
db_clear_private_heap (THREAD_ENTRY * thread_p, unsigned int heap_id)
{
  if (heap_id == 0)
    {
#if defined (SERVER_MODE)
      heap_id = css_get_private_heap (thread_p);
#else /* SERVER_MODE */
      heap_id = private_heap_id;
#endif /* SERVER_MODE */
    }

  if (heap_id)
    {
      hl_clear_ostk_heap (heap_id);
    }
}

/*
 * db_change_private_heap -  change private heap
 *    return: old private heap id
 *    heap_id(in): heap id
 */
unsigned int
db_change_private_heap (THREAD_ENTRY * thread_p, unsigned int heap_id)
{
  unsigned int old_heap_id;

#if defined (SERVER_MODE)
  old_heap_id = css_set_private_heap (thread_p, heap_id);
#else /* SERVER_MODE */
  old_heap_id = private_heap_id;
  if (db_on_server)
    {
      private_heap_id = heap_id;
    }
#endif
  return old_heap_id;
}

/*
 * db_replace_private_heap () - replace a thread specific heap
 *   return: old memory heap identifier
 *
 */
unsigned int
db_replace_private_heap (THREAD_ENTRY * thread_p)
{
  unsigned int old_heap_id, heap_id;

#if defined (SERVER_MODE)
  old_heap_id = css_get_private_heap (thread_p);
#else /* SERVER_MODE */
  old_heap_id = private_heap_id;
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  heap_id = hl_register_ostk_heap (DEFAULT_OBSTACK_CHUNK_SIZE);
  css_set_private_heap (thread_p, heap_id);
#else /* SERVER_MODE */
  if (db_on_server)
    {
      heap_id = hl_register_ostk_heap (DEFAULT_OBSTACK_CHUNK_SIZE);
      private_heap_id = heap_id;
    }
#endif /* SERVER_MODE */
  return old_heap_id;
}

/*
 * db_destroy_private_heap () - destroy a thread specific heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_private_heap (THREAD_ENTRY * thread_p, unsigned int heap_id)
{
  if (heap_id == 0)
    {
#if defined (SERVER_MODE)
      heap_id = css_get_private_heap (thread_p);
#else /* SERVER_MODE */
      heap_id = private_heap_id;
#endif /* SERVER_MODE */
    }

  if (heap_id)
    {
      hl_unregister_ostk_heap (heap_id);
    }
}

/*
 * db_private_alloc () - call allocation function for current private heap
 *   return: allocated memory pointer
 *   thrd(in): thread conext if it is server, otherwise NULL
 *   size(in): size to allocate
 */
void *
db_private_alloc (void *thrd, size_t size)
{
#if defined (CS_MODE)
  return db_ws_alloc (size);
#elif defined (SERVER_MODE)
  unsigned int heap_id;
  void *ptr = NULL;

  if (size <= 0)
    return NULL;

  heap_id = (thrd ? ((THREAD_ENTRY *) thrd)->private_heap_id :
	     css_get_private_heap (NULL));

  if (heap_id)
    {
      ptr = hl_ostk_alloc (heap_id, size);
    }
  else
    {
      ptr = malloc (size);
    }
  return ptr;
#else /* SA_MODE */
  if (!db_on_server)
    {
      return db_ws_alloc (size);
    }
  else
    {
      unsigned int heap_id;
      void *ptr = NULL;

      if (size <= 0)
	return NULL;

      heap_id = private_heap_id;

      if (heap_id)
	{
	  ptr = hl_ostk_alloc (heap_id, size);
	}
      else
	{
	  ptr = malloc (size);
	}
      return ptr;
    }
#endif /* SA_MODE */
}

/*
 * db_private_realloc () - call re-allocation function for current private heap
 *   return: allocated memory pointer
 *   thrd(in): thread conext if it is server, otherwise NULL
 *   ptr(in): memory pointer to reallocate
 *   size(in): size to allocate
 */
void *
db_private_realloc (void *thrd, void *ptr, size_t size)
{
#if defined (CS_MODE)
  return db_ws_realloc (ptr, size);
#elif defined (SERVER_MODE)
  unsigned int heap_id;
  void *new_ptr = NULL;

  if (size <= 0)
    return NULL;

  heap_id = (thrd ? ((THREAD_ENTRY *) thrd)->private_heap_id :
	     css_get_private_heap (NULL));

  if (heap_id)
    {
#if defined(NDEBUG)
      if (ptr == NULL)
	{
	  new_ptr = hl_ostk_alloc (heap_id, size);
	}
      else
	{
	  new_ptr = hl_ostk_realloc (heap_id, ptr, size);
	}
#else /* NDEBUG */
      new_ptr = hl_ostk_realloc (heap_id, ptr, size);
#endif /* NDEBUG */
    }
  else
    {
      new_ptr = realloc (ptr, size);
    }
  return new_ptr;
#else /* SA_MODE */
  if (!db_on_server)
    {
      return db_ws_realloc (ptr, size);
    }
  else
    {
      unsigned int heap_id;
      void *new_ptr = NULL;

      if (size <= 0)
	return NULL;

      heap_id = private_heap_id;

      if (heap_id)
	{
#if defined(NDEBUG)
	  if (ptr == NULL)
	    {
	      new_ptr = hl_ostk_alloc (heap_id, size);
	    }
	  else
	    {
	      new_ptr = hl_ostk_realloc (heap_id, ptr, size);
	    }
#else /* NDEBUG */
          new_ptr = hl_ostk_realloc (heap_id, ptr, size);
#endif /* NDEBUG */
	}
      else
	{
	  new_ptr = realloc (ptr, size);
	}
      return new_ptr;
    }
#endif /* SA_MODE */
}

/*
 * db_private_free () - call free function for current private heap
 *   return:
 *   thrd(in): thread conext if it is server, otherwise NULL
 *   ptr(in): memory pointer to free
 */
void
db_private_free (void *thrd, void *ptr)
{
#if defined (CS_MODE)
  db_ws_free (ptr);
#elif defined (SERVER_MODE)
  unsigned int heap_id;

  if (ptr == NULL)
    return;

  heap_id = (thrd ? ((THREAD_ENTRY *) thrd)->private_heap_id :
	     css_get_private_heap (NULL));

  if (heap_id)
    {
      // hl_ostk_free (heap_id, ptr);
    }
  else
    {
      free_and_init (ptr);
    }
#else /* SA_MODE */
  if (!db_on_server)
    {
      db_ws_free (ptr);
    }
  else
    {
      unsigned int heap_id;

      if (ptr == NULL)
	return;

      heap_id = private_heap_id;

      if (heap_id)
	{
	  // hl_ostk_free (heap_id, ptr);
	}
      else
	{
	  free_and_init (ptr);
	}
    }
#endif /* SA_MODE */
}
