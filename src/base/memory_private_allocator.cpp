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

//
// memory_private_allocator.cpp - extension to memory_alloc.h private allocation
//

#include "memory_private_allocator.hpp"

#if defined (SERVER_MODE)
#include "thread_manager.hpp"
#endif // SERVER_MODE
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmem
{
  void
  private_block_allocate (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = (char *) db_private_alloc (NULL, (int) size);
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// no change
      }
    else
      {
	size_t new_size = b.dim;
	while (new_size < size)
	  {
	    new_size *= 2;
	  }
	char *new_ptr = (char *) db_private_realloc (NULL, b.ptr, (int) new_size);
	if (new_ptr != NULL)
	  {
	    b.ptr = new_ptr;
	    b.dim = new_size;
	  }
	else
	  {
	    assert (false);
	  }
      }
  }

  void
  private_block_deallocate (block &b)
  {
    if (b.ptr != NULL)
      {
	db_private_free (NULL, b.ptr);
      }
    b.ptr = NULL;
    b.dim = 0;
  }

  const block_allocator PRIVATE_BLOCK_ALLOCATOR { private_block_allocate, private_block_deallocate };

  //
  // private allocation helper functions
  //
  HL_HEAPID
  get_private_heapid (cubthread::entry *&thread_p)
  {
#if defined (SERVER_MODE)
    if (thread_p == NULL)
      {
	thread_p = &cubthread::get_entry ();
      }
    return thread_p->private_heap_id;
#else // not SERVER_MODE
    (void) thread_p;    // not used
    return 0;
#endif // not SERVER_MODE
  }

  void *
  private_heap_allocate (cubthread::entry *thread_p, HL_HEAPID heapid, size_t size)
  {
#if defined (SERVER_MODE)
    if (heapid != thread_p->private_heap_id)
      {
	/* this is not something we should do! */
	assert (false);

	HL_HEAPID save_heapid = db_private_set_heapid_to_thread (thread_p, heapid);
	void *p = db_private_alloc (thread_p, size);
	(void) db_private_set_heapid_to_thread (thread_p, save_heapid);
	return p;
      }
    else
#endif // SERVER_MODE
      {
	return db_private_alloc (thread_p, size);
      }
  }

  void
  private_heap_deallocate (cubthread::entry *thread_p, HL_HEAPID heapid, void *ptr)
  {
#if defined (SERVER_MODE)
    if (heapid != thread_p->private_heap_id)
      {
	/* this is not something we should do! */
	assert (false);

	HL_HEAPID save_heapid = db_private_set_heapid_to_thread (thread_p, heapid);
	db_private_free (thread_p, ptr);
	(void) db_private_set_heapid_to_thread (thread_p, save_heapid);
      }
    else
#endif // SERVER_MODE
      {
	db_private_free (thread_p, ptr);
      }
  }

  void
  register_private_allocator (cubthread::entry *thread_p)
  {
#if defined (SERVER_MODE) && !defined (NDEBUG)
    thread_p->count_private_allocators++;
#else
    (void) thread_p;
#endif
  }

  void
  deregister_private_allocator (cubthread::entry *thread_p)
  {
#if defined (SERVER_MODE) && !defined (NDEBUG)
    thread_p->count_private_allocators--;
#else
    (void) thread_p;
#endif
  }
} // namespace cubmem
