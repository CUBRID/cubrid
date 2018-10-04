/* -*- C++ -*- */

#include <map>
#include "stlallocator.h"

#ifndef _SANITYCHECKHEAP_H_
#define _SANITYCHECKHEAP_H_

/**
 * @class SanityCheckHeap
 * @brief Checks for memory allocation usage errors at runtime.
 * See the "error messages" for the kinds of errors this layer can catch.
 * @author Emery Berger
 */

#include "mallocheap.h"
#include "zoneheap.h"
#include "stlallocator.h"
#include "mmapheap.h"

/* *INDENT-OFF* */
namespace HL
{

  template <class SuperHeap>
  class SanityCheckHeap : public SuperHeap
  {
    private:

      /// Define a local allocator that lets us use the SuperHeap for the map.
      /// This approach lets us use SanityCheckHeaps when we're replacing malloc.

      // The objects are pairs, mapping void * pointers to sizes.
      typedef std::pair<const intptr_t, size_t> objType;

      // The heap is a simple freelist heap.
      typedef HL::FreelistHeap<HL::ZoneHeap<HL::MmapHeap, 16384> > heapType;

      // And we wrap it up so it can be used as an STL allocator.
      typedef HL::STLAllocator<objType, heapType> localAllocator;

      typedef std::less<intptr_t> localComparator;

      /// A map of pointers to objects and their allocated sizes.
      typedef std::map<intptr_t, size_t, localComparator, localAllocator> mapType;

      /// A freed object has a special size, -1.
      enum { FREED = -1 };

      /**
       * @brief "Error messages", used in asserts.
       * These must all equal zero.
       */
      enum { MALLOC_RETURNED_ALLOCATED_OBJECT = 0,
	     FREE_CALLED_ON_OBJECT_I_NEVER_ALLOCATED = 0,
	     FREE_CALLED_TWICE_ON_SAME_OBJECT = 0
	   };

    public:

      inline void *malloc (size_t sz)
      {
	void *ptr = SuperHeap::malloc (sz);
	if (ptr == NULL)
	  {
	    return NULL;
	  }
	// Fill the space with a known value.
	memset (ptr, 'A', sz);
	// Record this object as allocated.
	mapType::iterator i;
	// Look for this object in the map of allocated objects.
	i = allocatedObjects.find ((intptr_t) ptr);
	if (i == allocatedObjects.end())
	  {
	    // We didn't find it (this is good).
	    // Add the tuple (ptr, sz).
	    allocatedObjects.insert (std::pair<intptr_t, int> ((intptr_t) ptr, sz));
	  }
	else
	  {
	    // We found it.
	    // It really should have been freed.
	    if ((*i).second != FREED)
	      {
		// This object is still in use!
		assert (MALLOC_RETURNED_ALLOCATED_OBJECT);
		return NULL;
	      }
	    else
	      {
		// This object has been freed. Mark it as allocated.
		(*i).second = sz;
	      }
	  }
	return ptr;
      }

      inline void free (void *ptr)
      {
	// Look for this object in the list of allocated objects.
	mapType::iterator i;
	i = allocatedObjects.find ((intptr_t) ptr);
	if (i == allocatedObjects.end())
	  {
	    assert (FREE_CALLED_ON_OBJECT_I_NEVER_ALLOCATED);
	    return;
	  }
	// We found the object. It should not have been freed already.
	if ((*i).second == FREED)
	  {
	    // Oops, this object WAS freed before.
	    assert (FREE_CALLED_TWICE_ON_SAME_OBJECT);
	    return;
	  }
	// Fill the space with a known value.
	memset (ptr, 'F', (*i).second);
	// Really free the pointer.
	(*i).second = FREED;
	SuperHeap::free (ptr);
      }

    private:

      /// A map of tuples: (obj address, size).
      mapType allocatedObjects;
  };

};
/* *INDENT-ON* */

#endif
