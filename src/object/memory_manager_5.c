/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * TODO: include this file to base/memory_manager_2.c
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "thread_impl.h"
#include "memory_manager_2.h"
#include "memory_manager_4.h"
#include "customheaps.h"
#if !defined (SERVER_MODE)
#include "quick_fit.h"

extern unsigned int db_on_server;
#endif /* SERVER_MODE */

#define DEFAULT_OBSTACK_CHUNK_SIZE	32768 /* 1024 x 32 */

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
