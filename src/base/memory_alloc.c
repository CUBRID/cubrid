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

#include "memory_alloc.h"

#include "error_manager.h"
#include "resource_tracker.hpp"
#include "customheaps.h"

#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif // SERVER_MODE

#define DEFAULT_OBSTACK_CHUNK_SIZE      32768	/* 1024 x 32 */

#if !defined (SERVER_MODE)
extern unsigned int db_on_server;
HL_HEAPID private_heap_id = 0;	/* for SA's server-side */
HL_HEAPID ws_heap_id = 0;	/* for both SA's and CS's workspace */
#endif /* SERVER_MODE */

#if defined(SA_MODE)
typedef struct private_malloc_header_s PRIVATE_MALLOC_HEADER;
struct private_malloc_header_s
{
  unsigned int magic;
  int alloc_type;
};

#define PRIVATE_MALLOC_HEADER_MAGIC 0xafdaafdaU

enum
{
  PRIVATE_ALLOC_TYPE_LEA = 1,
  PRIVATE_ALLOC_TYPE_WS = 2
};

#define PRIVATE_MALLOC_HEADER_ALIGNED_SIZE \
  ((sizeof(PRIVATE_MALLOC_HEADER) + 7) & ~7)

#define private_request_size(s) \
  (PRIVATE_MALLOC_HEADER_ALIGNED_SIZE + (s))

#define private_hl2user_ptr(ptr) \
  (void *)((char *)(ptr) + PRIVATE_MALLOC_HEADER_ALIGNED_SIZE)

#define private_user2hl_ptr(ptr) \
  (PRIVATE_MALLOC_HEADER *)((char *)(ptr) - PRIVATE_MALLOC_HEADER_ALIGNED_SIZE)
#endif /* SA_MODE */

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
  heap_id = hl_register_lea_heap ();
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
#elif defined (CS_MODE)
      heap_id = ws_heap_id;
#else
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
#elif defined (CS_MODE)
  old_heap_id = ws_heap_id;
#else
  if (db_on_server)
    {
      old_heap_id = private_heap_id;
      private_heap_id = heap_id;
    }
  else
    {
      old_heap_id = ws_heap_id;
      ws_heap_id = heap_id;
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
#elif defined (CS_MODE)
  old_heap_id = ws_heap_id;
#else /* SERVER_MODE */
  old_heap_id = private_heap_id;
#endif /* SERVER_MODE */

  heap_id = db_create_private_heap ();

#if defined (SERVER_MODE)
  db_private_set_heapid_to_thread (thread_p, heap_id);
#elif defined (CS_MODE)
  ws_heap_id = heap_id;
#else
  if (db_on_server)
    {
      private_heap_id = heap_id;
    }
  else
    {
      ws_heap_id = heap_id;
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
#elif defined (CS_MODE)
      heap_id = ws_heap_id;
#else /* SERVER_MODE */
      heap_id = (db_on_server) ? private_heap_id : ws_heap_id;
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
  void *ptr = NULL;

  if (size == 0)
    {
      return NULL;
    }

#if defined (SERVER_MODE)
  HL_HEAPID heap_id = db_private_get_heapid_from_thread (thrd);
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
#elif defined (CS_MODE)
  if (ws_heap_id == 0)
    {
      /* not initialized yet */
      ws_heap_id = db_create_private_heap ();
    }

  if (ws_heap_id)
    {
      ptr = hl_lea_alloc (ws_heap_id, size);
    }
#else /* SERVER_MODE */
  HL_HEAPID *heap_id = db_on_server ? &private_heap_id : &ws_heap_id;

  if (!db_on_server && *heap_id == 0)
    {
      *heap_id = db_create_private_heap ();
    }

  if (*heap_id)
    {
      PRIVATE_MALLOC_HEADER *h = NULL;
      size_t req_sz = private_request_size (size);
      h = (PRIVATE_MALLOC_HEADER *) hl_lea_alloc (private_heap_id, req_sz);
      if (h != NULL)
	{
	  h->magic = PRIVATE_MALLOC_HEADER_MAGIC;
	  h->alloc_type = db_on_server ? PRIVATE_ALLOC_TYPE_LEA : PRIVATE_ALLOC_TYPE_WS;
	  ptr = private_hl2user_ptr (h);
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
    }
#endif /* SA_MODE */
  return ptr;
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
  void *new_ptr = NULL;

  if (size == 0)
    {
      return NULL;
    }

#if defined (SERVER_MODE)
  HL_HEAPID heap_id = db_private_get_heapid_from_thread (thrd);

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

#elif defined (CS_MODE)
  if (ws_heap_id == 0)
    {
      /* not initialized yet */
      ws_heap_id = db_create_private_heap ();
    }

  if (ws_heap_id && (size > 0))
    {
      new_ptr = hl_lea_realloc (ws_heap_id, ptr, size);
    }
#else /* SA_MODE */
  if (ptr == NULL)
    {
      return db_private_alloc (thrd, size);
    }

  HL_HEAPID *heap_id = db_on_server ? &private_heap_id : &ws_heap_id;

  if (!db_on_server && *heap_id == 0)
    {
      *heap_id = db_create_private_heap ();
    }

  if (*heap_id)
    {
      PRIVATE_MALLOC_HEADER *h;

      h = private_user2hl_ptr (ptr);
      if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
	{
	  return NULL;
	}

      PRIVATE_MALLOC_HEADER *new_h;
      size_t req_sz;

      req_sz = private_request_size (size);
      new_h = (PRIVATE_MALLOC_HEADER *) hl_lea_realloc (*heap_id, h, req_sz);
      if (new_h == NULL)
	{
	  return NULL;
	}

      /* make sure ptr was allocated in the same mode (db_on_server) */
      assert ((db_on_server && (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA))
	      || (!db_on_server && (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)));

      new_ptr = private_hl2user_ptr (new_h);
    }
  else
    {
      new_ptr = realloc (ptr, size);
      if (new_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	}
    }
#endif /* SA_MODE */

  return new_ptr;
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
  if (ptr == NULL)
    {
      return;
    }

#if defined (SERVER_MODE)
  HL_HEAPID heap_id = db_private_get_heapid_from_thread (thrd);

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

#elif defined (CS_MODE)
  assert (ws_heap_id != 0);

  if (ws_heap_id)
    {
      hl_lea_free (ws_heap_id, ptr);
    }

#else /* SA_MODE */
  HL_HEAPID *heap_id = db_on_server ? &private_heap_id : &ws_heap_id;

  if (*heap_id == 0)
    {
      free (ptr);
    }
  else
    {
      PRIVATE_MALLOC_HEADER *h = private_user2hl_ptr (ptr);
      if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
	{
	  /* assertion point */
	  assert (false);
	  return;
	}

      /* make sure ptr was allocated in the same mode (db_on_server) */
      assert ((db_on_server && (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA))
	      || (!db_on_server && (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)));

      hl_lea_free (*heap_id, h);
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
HL_HEAPID
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
 * db_private_set_heapid_to_thread() -
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
