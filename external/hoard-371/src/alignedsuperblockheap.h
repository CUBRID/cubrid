// -*- C++ -*-

#ifndef _ALIGNEDSUPERBLOCKHEAP_H_
#define _ALIGNEDSUPERBLOCKHEAP_H_

#include "mmapheap.h"
#include "sassert.h"

#include "conformantheap.h"
#include "lockedheap.h"
#include "fixedrequestheap.h"
#include "dllist.h"

// Always requests aligned superblocks.
#include "alignedmmap.h"

namespace Hoard {

template <size_t SuperblockSize>
class SuperblockStore {
public:

  enum { Alignment = AlignedMmap<SuperblockSize>::Alignment };

  void * malloc (size_t sz) {
    sz = sz; // to avoid warning.
    assert (sz == SuperblockSize);
    if (_freeSuperblocks.isEmpty()) {
      // Get more memory.
      void * ptr = _superblockSource.malloc (ChunksToGrab * SuperblockSize);
      if (!ptr) {
	return NULL;
      }
      char * p = (char *) ptr;
      for (int i = 0; i < ChunksToGrab; i++) {
	_freeSuperblocks.insert ((DLList::Entry *) p);
	p += SuperblockSize;
      }
    }
    return _freeSuperblocks.get();
  }

  void free (void * ptr) {
    _freeSuperblocks.insert ((DLList::Entry *) ptr);
  }

private:

#if defined(__SVR4)
  enum { ChunksToGrab = 63 };
#else
  enum { ChunksToGrab = 1 };
#endif

  AlignedMmap<SuperblockSize> _superblockSource;
  DLList _freeSuperblocks;

};

}


namespace Hoard {

template <class TheLockType,
	  size_t SuperblockSize>
class AlignedSuperblockHeapHelper :
  public ConformantHeap<HL::LockedHeap<TheLockType,
				       FixedRequestHeap<SuperblockSize, 
							// AlignedMmap<SuperblockSize> > > > {};
				       SuperblockStore<SuperblockSize> > > > {};


template <class TheLockType,
	  size_t SuperblockSize>
class AlignedSuperblockHeap :
  public AlignedSuperblockHeapHelper<TheLockType, SuperblockSize> {

  HL::sassert<(AlignedSuperblockHeapHelper<TheLockType, SuperblockSize>::Alignment % SuperblockSize == 0)> EnsureProperAlignment;

};

};

#endif
