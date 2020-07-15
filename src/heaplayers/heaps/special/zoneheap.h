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

/**
 * @class ZoneHeap
 * @brief A zone (or arena, or region) based allocator.
 * @author Emery Berger
 * @date June 2000
 *
 * Uses the superclass to obtain large chunks of memory that are only
 * returned when the heap itself is destroyed.
 *
*/
#ifndef HL_ZONEHEAP_H
#define HL_ZONEHEAP_H

#include <assert.h>

#include "utility/align.h"
#include "wrappers/mallocinfo.h"

namespace HL {

  template <class SuperHeap, size_t ChunkSize>
  class ZoneHeap : public SuperHeap {
  public:

    enum { Alignment = SuperHeap::Alignment };

    ZoneHeap()
      : _sizeRemaining (0),
	_currentArena (NULL),
	_pastArenas (NULL)
    {
      m_chkSize = ChunkSize;
    }

    ~ZoneHeap()
    {
      // printf ("deleting arenas!\n");
      // Delete all of our arenas.
      Arena * ptr = _pastArenas;
      while (ptr != NULL) {
	void * oldPtr = (void *) ptr;
	ptr = ptr->nextArena;
	//printf ("deleting %x\n", ptr);
	SuperHeap::free (oldPtr);
      }
      if (_currentArena != NULL)
	//printf ("deleting %x\n", _currentArena);
	SuperHeap::free ((void *) _currentArena);
    }

    inline void reset(const int chkSize)
    {
      m_chkSize = chkSize;
    }

    inline void * malloc (size_t sz) {
      void * ptr = zoneMalloc (sz);
      //      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }

    /// Free in a zone allocator is a no-op.
    inline void free (void *) {}

    /// Remove in a zone allocator is a no-op.
    inline int remove (void *) { return 0; }


  private:

    int m_chkSize;

    ZoneHeap (const ZoneHeap&);
    ZoneHeap& operator=(const ZoneHeap&);

    inline void * zoneMalloc (size_t sz) {
      void * ptr;
      // Round up size to an aligned value.
      sz = HL::align<HL::MallocInfo::Alignment>(sz);
      // Get more space in our arena if there's not enough room in this one.
      if ((_currentArena == NULL) || (_sizeRemaining < sz)) {
	// First, add this arena to our past arena list.
	if (_currentArena != NULL) {
	  _currentArena->nextArena = _pastArenas;
	  _pastArenas = _currentArena;
	}
	// Now get more memory.
	size_t allocSize = m_chkSize;
	if (allocSize < sz) {
	  allocSize = sz;
	}
	_currentArena =
	  (Arena *) SuperHeap::malloc (allocSize + sizeof(Arena));
	if (_currentArena == NULL) {
	  return NULL;
	}
	_currentArena->arenaSpace = (char *) (_currentArena + 1);
	_currentArena->nextArena = NULL;
	_sizeRemaining = allocSize;
      }
      // Bump the pointer and update the amount of memory remaining.
      _sizeRemaining -= sz;
      ptr = _currentArena->arenaSpace;
      _currentArena->arenaSpace += sz;
      assert (ptr != NULL);
      //      assert ((size_t) ptr % SuperHeap::Alignment == 0);
      return ptr;
    }
  
    class Arena {
    public:
      Arena() {
	static_assert((sizeof(Arena) % HL::MallocInfo::Alignment == 0),
		      "Alignment must match Arena size.");
      }

      Arena * nextArena;
      char * arenaSpace;
    };
    
    /// Space left in the current arena.
    size_t _sizeRemaining;

    /// The current arena.
    Arena * _currentArena;

    /// A linked list of past arenas.
    Arena * _pastArenas;
  };

}

#endif
