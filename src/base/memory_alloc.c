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
 * memory_alloc.c - Memory allocation module
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "set_object.h"
#include "misc_string.h"
#include "object_domain.h"
#include "dbtype.h"
#include "memory_alloc.h"
#include "util_func.h"
#include "error_manager.h"
#include "intl_support.h"
#include "resource_tracker.hpp"
#include "customheaps.h"
#if !defined (SERVER_MODE)
#include "quick_fit.h"
#endif /* SERVER_MODE */
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#endif // SERVER_MODE
#if defined (SERVER_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
#endif // SERVER_MODE

#define DEFAULT_OBSTACK_CHUNK_SIZE      32768	/* 1024 x 32 */

#if !defined (SERVER_MODE)
extern unsigned int db_on_server;
HL_HEAPID private_heap_id = 0;
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
static HL_HEAPID db_private_get_heapid_from_thread (REFPTR (THREAD_ENTRY, thread_p));
#endif // SERVER_MODE

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
    {
      if (*s == '\0')
	{
	  return 0;
	}
    }

  if (*s == '\0')
    {
      while (*t != '\0')
	{
	  if (*t++ != ' ')
	    {
	      return -1;
	    }
	}
      return 0;
    }
  else if (*t == '\0')
    {
      while (*s != '\0')
	{
	  if (*s++ != ' ')
	    {
	      return 1;
	    }
	}
      return 0;
    }
  else
    {
      return (*(unsigned const char *) s < *(unsigned const char *) t) ? -1 : 1;
    }
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

  cmp_val = intl_identifier_ncasecmp (s, t, (int) min_length);

  /* If not equal for shorter length, return */
  if (cmp_val)
    {
      return cmp_val;
    }

  /* If equal and same size, return */
  if (s_length == t_length)
    {
      return 0;
    }

  /* If equal for shorter length and not same size, look for trailing blanks */
  s += min_length;
  t += min_length;

  if (*s == '\0')
    {
      while (*t != '\0')
	{
	  if (*t++ != ' ')
	    {
	      return -1;
	    }
	}
      return 0;
    }
  else
    {
      while (*s != '\0')
	{
	  if (*s++ != ' ')
	    {
	      return 1;
	    }
	}
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
  if (n >= (int) sizeof (double))
    {
      return (int) sizeof (double);
    }
  else if (n >= (int) sizeof (void *))
    {
      return (int) sizeof (void *);
    }
  else if (n >= (int) sizeof (int))
    {
      return (int) sizeof (int);
    }
  else if (n >= (int) sizeof (short))
    {
      return (int) sizeof (short);
    }
  else
    {
      return 1;
    }
}

/*
 * db_align_to () - Return the least multiple of 'alignment' that is greater
 * than or equal to 'n'.
 *   return:
 *   n(in):
 *   alignment(in):
 */
int
db_align_to (int n, int alignment)
{
  /*
   * Return the least multiple of 'alignment' that is greater than or
   * equal to 'n'.  'alignment' must be a power of 2.
   */
  return (n + alignment - 1) & ~(alignment - 1);
}

/*
 * db_create_ostk_heap () - create an obstack heap
 *   return: memory heap identifier
 *   chunk_size(in):
 */
HL_HEAPID
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
db_destroy_ostk_heap (HL_HEAPID heap_id)
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
db_ostk_alloc (HL_HEAPID heap_id, size_t size)
{
  void *ptr = NULL;
  if (heap_id && size > 0)
    {
      ptr = hl_ostk_alloc (heap_id, size);
    }
  return ptr;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_ostk_free () - call free function for the ostk heap
 *   return:
 *   heap_id(in): memory heap identifier
 *   ptr(in): memory pointer to free
 */
void
db_ostk_free (HL_HEAPID heap_id, void *ptr)
{
  if (heap_id && ptr)
    {
      hl_ostk_free (heap_id, ptr);
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * db_create_private_heap () - create a thread specific heap
 *   return: memory heap identifier
 */
HL_HEAPID
db_create_private_heap (void)
{
  HL_HEAPID heap_id = 0;
#if defined (SERVER_MODE)
  heap_id = hl_register_lea_heap ();
#else /* SERVER_MODE */
  if (db_on_server)
    {
      heap_id = hl_register_lea_heap ();
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
db_clear_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id)
{
  if (heap_id == 0)
    {
#if defined (SERVER_MODE)
      heap_id = db_private_get_heapid_from_thread (thread_p);
#else /* SERVER_MODE */
      heap_id = private_heap_id;
#endif /* SERVER_MODE */
    }

  if (heap_id)
    {
      hl_clear_lea_heap (heap_id);
    }
}

/*
 * db_change_private_heap () - change private heap
 *    return: old private heap id
 *    heap_id(in): heap id
 */
HL_HEAPID
db_change_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id)
{
  HL_HEAPID old_heap_id;

#if defined (SERVER_MODE)
  old_heap_id = db_private_set_heapid_to_thread (thread_p, heap_id);
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
HL_HEAPID
db_replace_private_heap (THREAD_ENTRY * thread_p)
{
  HL_HEAPID old_heap_id, heap_id;

#if defined (SERVER_MODE)
  old_heap_id = db_private_get_heapid_from_thread (thread_p);
#else /* SERVER_MODE */
  old_heap_id = private_heap_id;
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  heap_id = db_create_private_heap ();
  db_private_set_heapid_to_thread (thread_p, heap_id);
#else /* SERVER_MODE */
  if (db_on_server)
    {
      heap_id = db_create_private_heap ();
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
db_destroy_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id)
{
  if (heap_id == 0)
    {
#if defined (SERVER_MODE)
      heap_id = db_private_get_heapid_from_thread (thread_p);
#else /* SERVER_MODE */
      heap_id = private_heap_id;
#endif /* SERVER_MODE */
    }

  if (heap_id)
    {
      hl_unregister_lea_heap (heap_id);
    }
}

/*
 * db_private_alloc () - call allocation function for current private heap
 *   return: allocated memory pointer
 *   thrd(in): thread context if it is available, otherwise NULL; if called
 *             on the server and this parameter is NULL, the function will
 *             determine the appropriate thread context
 *   size(in): size to allocate
 */

/* dummy definition for Windows */
#if defined(WINDOWS)
#if !defined(NDEBUG)
void *
db_private_alloc_release (THREAD_ENTRY * thrd, size_t size, bool rc_track)
{
  return NULL;
}
#else
void *
db_private_alloc_debug (THREAD_ENTRY * thrd, size_t size, bool rc_track, const char *caller_file, int caller_line)
{
  return NULL;
}
#endif
#endif

#if !defined(NDEBUG)
void *
db_private_alloc_debug (THREAD_ENTRY * thrd, size_t size, bool rc_track, const char *caller_file, int caller_line)
#else /* NDEBUG */
void *
db_private_alloc_release (THREAD_ENTRY * thrd, size_t size, bool rc_track)
#endif				/* NDEBUG */
{
#if !defined (CS_MODE)
  void *ptr = NULL;
#endif /* !CS_MODE */

#if defined (SERVER_MODE)
  HL_HEAPID heap_id;
#endif

  assert (size > 0);

#if defined (CS_MODE)
  return db_ws_alloc (size);
#elif defined (SERVER_MODE)
  if (size <= 0)
    {
      return NULL;
    }

  heap_id = db_private_get_heapid_from_thread (thrd);

  if (heap_id)
    {
      ptr = hl_lea_alloc (heap_id, size);
    }
  else
    {
      ptr = malloc (size);
      if (ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	}
    }

#if !defined (NDEBUG)
  if (rc_track && heap_id != 0)
    {
      if (ptr != NULL)
	{
	  thread_get_thread_entry_info ()->get_alloc_tracker ().increment (caller_file, caller_line, ptr);
	}
    }
#endif /* !NDEBUG */

  return ptr;
#else /* SA_MODE */
  if (!db_on_server)
    {
      return db_ws_alloc (size);
    }
  else
    {
      if (size <= 0)
	{
	  return NULL;
	}

      if (private_heap_id)
	{
	  PRIVATE_MALLOC_HEADER *h = NULL;
	  size_t req_sz;

	  req_sz = private_request_size (size);
	  h = (PRIVATE_MALLOC_HEADER *) hl_lea_alloc (private_heap_id, req_sz);

	  if (h != NULL)
	    {
	      h->magic = PRIVATE_MALLOC_HEADER_MAGIC;
	      h->alloc_type = PRIVATE_ALLOC_TYPE_LEA;
	      return private_hl2user_ptr (h);
	    }
	  else
	    {
	      return NULL;
	    }
	}
      else
	{
	  ptr = malloc (size);
	  if (ptr == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	    }
	  return ptr;
	}
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
/* dummy definition for Windows */
#if defined(WINDOWS)
#if !defined(NDEBUG)
void *
db_private_realloc_release (THREAD_ENTRY * thrd, void *ptr, size_t size, bool rc_track)
{
  return NULL;
}
#else
void *
db_private_realloc_debug (THREAD_ENTRY * thrd, void *ptr, size_t size, bool rc_track, const char *caller_file,
			  int caller_line)
{
  return NULL;
}
#endif
#endif

#if !defined(NDEBUG)
void *
db_private_realloc_debug (THREAD_ENTRY * thrd, void *ptr, size_t size, bool rc_track, const char *caller_file,
			  int caller_line)
#else /* NDEBUG */
void *
db_private_realloc_release (THREAD_ENTRY * thrd, void *ptr, size_t size, bool rc_track)
#endif				/* NDEBUG */
{
#if !defined (CS_MODE)
  void *new_ptr = NULL;
#endif /* !CS_MODE */

#if defined (SERVER_MODE)
  HL_HEAPID heap_id;
#endif

#if defined (CS_MODE)
  return db_ws_realloc (ptr, size);
#elif defined (SERVER_MODE)
  if (size <= 0)
    {
      return NULL;
    }

  heap_id = db_private_get_heapid_from_thread (thrd);

  if (heap_id)
    {
      new_ptr = hl_lea_realloc (heap_id, ptr, size);
    }
  else
    {
      new_ptr = realloc (ptr, size);
      if (new_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	}
    }

#if !defined (NDEBUG)
  if (rc_track && heap_id != 0 && new_ptr != ptr)
    {
      /* remove old pointer from track meter */
      if (ptr != NULL)
	{
	  thread_get_thread_entry_info ()->get_alloc_tracker ().decrement (ptr);
	}
      /* add new pointer to track meter */
      if (new_ptr != NULL)
	{
	  thread_get_thread_entry_info ()->get_alloc_tracker ().increment (caller_file, caller_line, new_ptr);
	}
    }
#endif /* !NDEBUG */

  return new_ptr;
#else /* SA_MODE */
  if (ptr == NULL)
    {
      return db_private_alloc (thrd, size);
    }

  if (!db_on_server)
    {
      return db_ws_realloc (ptr, size);
    }
  else
    {
      if (private_heap_id)
	{
	  PRIVATE_MALLOC_HEADER *h;

	  h = private_user2hl_ptr (ptr);
	  if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
	    {
	      return NULL;
	    }

	  if (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA)
	    {
	      PRIVATE_MALLOC_HEADER *new_h;
	      size_t req_sz;

	      req_sz = private_request_size (size);
	      new_h = (PRIVATE_MALLOC_HEADER *) hl_lea_realloc (private_heap_id, h, req_sz);
	      if (new_h == NULL)
		{
		  return NULL;
		}
	      return private_hl2user_ptr (new_h);
	    }
	  else if (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)
	    {
	      return db_ws_realloc (ptr, size);
	    }
	  else
	    {
	      return NULL;
	    }
	}
      else
	{
	  new_ptr = realloc (ptr, size);
	  if (new_ptr == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	    }
	  return new_ptr;
	}
    }
#endif /* SA_MODE */
}

/*
 * db_private_private_strdup () - duplicate string. memory for the duplicated
 *   string is obtanined by db_private_alloc
 *   return: pointer to duplicated str
 *   thrd(in): thread conext if it is server, otherwise NULL
 *   size(s): source string
 */
char *
db_private_strdup (THREAD_ENTRY * thrd, const char *s)
{
  char *cp;
  int len;

  /* fast return */
  if (s == NULL)
    {
      return NULL;
    }

  len = (int) strlen (s);
  cp = (char *) db_private_alloc (thrd, len + 1);

  if (cp != NULL)
    {
      memcpy (cp, s, len);
      cp[len] = '\0';
    }

  return cp;
}

/*
 * db_private_free () - call free function for current private heap
 *   return:
 *   thrd(in): thread conext if it is server, otherwise NULL
 *   ptr(in): memory pointer to free
 */

/* dummy definition for Windows */
#if defined(WINDOWS)
#if !defined(NDEBUG)
void
db_private_free_release (THREAD_ENTRY * thrd, void *ptr, bool rc_track)
{
  return;
}
#else
void
db_private_free_debug (THREAD_ENTRY * thrd, void *ptr, bool rc_track, const char *caller_file, int caller_line)
{
  return;
}
#endif
#endif

#if !defined(NDEBUG)
void
db_private_free_debug (THREAD_ENTRY * thrd, void *ptr, bool rc_track, const char *caller_file, int caller_line)
#else /* NDEBUG */
void
db_private_free_release (THREAD_ENTRY * thrd, void *ptr, bool rc_track)
#endif				/* NDEBUG */
{
#if defined (SERVER_MODE)
  HL_HEAPID heap_id;
#endif

  if (ptr == NULL)
    {
      return;
    }

#if defined (CS_MODE)
  db_ws_free (ptr);
#elif defined (SERVER_MODE)
  heap_id = db_private_get_heapid_from_thread (thrd);

  if (heap_id)
    {
      hl_lea_free (heap_id, ptr);
    }
  else
    {
      free (ptr);
    }

#if !defined (NDEBUG)
  if (rc_track && heap_id != 0)
    {
      if (ptr != NULL)
	{
	  thrd->get_alloc_tracker ().decrement (ptr);
	}
    }
#endif /* !NDEBUG */

#else /* SA_MODE */

  if (!db_on_server)
    {
      db_ws_free (ptr);
      return;
    }

  if (private_heap_id == 0)
    {
      free (ptr);
    }
  else
    {
      PRIVATE_MALLOC_HEADER *h;

      h = private_user2hl_ptr (ptr);
      if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
	{
	  /* assertion point */
	  return;
	}

      if (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA)
	{
	  hl_lea_free (private_heap_id, h);
	}
      else if (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)
	{
	  db_ws_free (ptr);	/* not h */
	}
      else
	{
	  return;
	}
    }
#endif /* SA_MODE */
}


void *
db_private_alloc_external (THREAD_ENTRY * thrd, size_t size)
{
#if !defined(NDEBUG)
  return db_private_alloc_debug (thrd, size, true, __FILE__, __LINE__);
#else /* NDEBUG */
  return db_private_alloc_release (thrd, size, false);
#endif /* NDEBUG */
}


void
db_private_free_external (THREAD_ENTRY * thrd, void *ptr)
{
#if !defined(NDEBUG)
  db_private_free_debug (thrd, ptr, true, __FILE__, __LINE__);
#else /* NDEBUG */
  db_private_free_release (thrd, ptr, false);
#endif /* NDEBUG */
}


void *
db_private_realloc_external (THREAD_ENTRY * thrd, void *ptr, size_t size)
{
#if !defined(NDEBUG)
  return db_private_realloc_debug (thrd, ptr, size, true, __FILE__, __LINE__);
#else /* NDEBUG */
  return db_private_realloc_release (thrd, ptr, size, false);
#endif /* NDEBUG */
}

#if defined (SERVER_MODE)
/*
 * os_malloc () -
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
#if !defined(NDEBUG)
void *
os_malloc_debug (size_t size, bool rc_track, const char *caller_file, int caller_line)
#else /* NDEBUG */
void *
os_malloc_release (size_t size, bool rc_track)
#endif				/* NDEBUG */
{
  void *ptr = NULL;

  assert (size > 0);

  ptr = malloc (size);
  if (ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
    }

#if !defined (NDEBUG)
  if (rc_track)
    {
      if (ptr != NULL)
	{
	  thread_get_thread_entry_info ()->get_alloc_tracker ().increment (caller_file, caller_line, ptr);
	}
    }
#endif /* !NDEBUG */

  return ptr;
}


/*
 * os_calloc () -
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
#if !defined(NDEBUG)
void *
os_calloc_debug (size_t n, size_t size, bool rc_track, const char *caller_file, int caller_line)
#else /* NDEBUG */
void *
os_calloc_release (size_t n, size_t size, bool rc_track)
#endif				/* NDEBUG */
{
  void *ptr = NULL;

  assert (size > 0);

  ptr = calloc (n, size);
  if (ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
    }

#if !defined (NDEBUG)
  if (rc_track)
    {
      if (ptr != NULL)
	{
	  thread_get_thread_entry_info ()->get_alloc_tracker ().increment (caller_file, caller_line, ptr);
	}
    }
#endif /* !NDEBUG */

  return ptr;
}

/*
 * os_free () -
 *   return:
 *   ptr(in): memory pointer to free
 */
#if !defined(NDEBUG)
void
os_free_debug (void *ptr, bool rc_track, const char *caller_file, int caller_line)
#else /* NDEBUG */
void
os_free_release (void *ptr, bool rc_track)
#endif				/* NDEBUG */
{
  free (ptr);

#if !defined (NDEBUG)
  if (rc_track)
    {
      if (ptr != NULL)
	{
	  thread_get_thread_entry_info ()->get_alloc_tracker ().decrement (ptr);
	}
    }
#endif /* !NDEBUG */
}

#if defined (SERVER_MODE)
/*
 * db_private_get_heapid_from_thread () -
 *   return: heap id
 *   thread_p(in/out): thread local entry; output is never nil
 */
static HL_HEAPID
db_private_get_heapid_from_thread (REFPTR (THREAD_ENTRY, thread_p))
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  assert (thread_p != NULL);

  return thread_p->private_heap_id;
}

/*
 * css_set_private_heap() -
 *   return:
 *   thread_p(in):
 *   heap_id(in):
 */
HL_HEAPID
db_private_set_heapid_to_thread (THREAD_ENTRY * thread_p, HL_HEAPID heap_id)
{
  HL_HEAPID old_heap_id = 0;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  old_heap_id = thread_p->private_heap_id;
  thread_p->private_heap_id = heap_id;

  return old_heap_id;
}
#endif // SERVER_MODE

#endif
