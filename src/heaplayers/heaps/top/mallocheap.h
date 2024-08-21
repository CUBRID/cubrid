/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2020 by Emery Berger
  http://www.emeryberger.com
  emery@cs.umass.edu
  
  Heap Layers is distributed under the terms of the Apache 2.0 license.

  You may obtain a copy of the License at
  http://www.apache.org/licenses/LICENSE-2.0

*/

#ifndef HL_MALLOCHEAP_H
#define HL_MALLOCHEAP_H

#include <cstdlib>

#if defined(__SVR4)
extern "C" size_t malloc_usable_size (void *);
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#else
// *INDENT-OFF*
extern "C" size_t malloc_usable_size (void *) throw ();
// *INDENT-ON*
#endif

/**
 * @class MallocHeap
 * @brief A "source heap" that uses malloc and free.
 */

#include "wrappers/mallocinfo.h"
#if defined(SERVER_MODE)
#include "memory_monitor_sr.hpp"
#endif /* SERVER_MODE */

namespace HL
{

  class MallocHeap
  {
  public:

    enum
    { Alignment = MallocInfo::Alignment };

    inline void *malloc (size_t sz)
    {
#if defined(SERVER_MODE) && !defined(_MSC_VER)
      if (mmon_is_memory_monitor_enabled ())
	{
	  void *ptr =::malloc (sz + cubmem::MMON_METAINFO_SIZE);
	  if (ptr != NULL)
	    {
	      mmon_add_stat ((char *) ptr, malloc_usable_size (ptr), __FILE__, __LINE__);
	    }
	  return ptr;
	}
      else
	{
#endif
	  return::malloc (sz);
#if defined(SERVER_MODE) && !defined(_MSC_VER)
	}
#endif
    }

    inline void free (void *ptr)
    {
#if defined(SERVER_MODE) && !defined(_MSC_VER)
      if (mmon_is_memory_monitor_enabled () && ptr != NULL)
	{
	  mmon_sub_stat ((char *) ptr);
	}
#endif
      ::free (ptr);
    }

#if defined(_MSC_VER)
    inline size_t getSize (void *ptr)
    {
      return::_msize (ptr);
    }
#elif defined(__APPLE__)
    inline size_t getSize (void *ptr)
    {
      return::malloc_size (ptr);
    }
#elif defined(__GNUC__) && !defined(__SVR4)
    inline size_t getSize (void *ptr)
    {
      return::malloc_usable_size (ptr);
    }
#endif

  };

}

#endif
