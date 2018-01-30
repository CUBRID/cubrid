//
// Custom Heap Memory Allocators
//

#include <stdlib.h>
#include <new>

#include "system.h"
#include "obstackheap.h"
#include "heaplayers.h"

using namespace HL;

volatile int anyThreadCreatedInHL = 1;

//
// Fixed Heap
//

// class definition
class TheFixedHeapType :
  public LockedHeap<SpinLockType,FreelistHeap<ZoneHeap<mallocHeap,0> > > {};

// initialize & finalize
UINTPTR
hl_register_fixed_heap (int chunk_size)
{
  TheFixedHeapType *th = new TheFixedHeapType;
  if (th)
    {
      th->reset (chunk_size);
      return (UINTPTR) th;
    }
  return 0;
}

void
hl_unregister_fixed_heap (UINTPTR heap_id)
{
  TheFixedHeapType *th = (TheFixedHeapType *) heap_id;
  if (th)
    {
      delete th;
    }
}

// alloc & free
void *
hl_fixed_alloc (UINTPTR heap_id, size_t sz)
{
  TheFixedHeapType *th = (TheFixedHeapType *) heap_id;
  if (th)
    {
      return th->malloc (sz);
    }
  return NULL;
}

void
hl_fixed_free (UINTPTR heap_id, void *ptr)
{
  TheFixedHeapType *th = (TheFixedHeapType *) heap_id;
  if (th)
    {
      th->free (ptr);
    }
}

//
// Obstack Heap
//

// class definition
class TheObstackHeapType :
  public SizeHeap<ObstackHeap<0,mallocHeap> > {};

// initialize & finalize
UINTPTR
hl_register_ostk_heap (int chunk_size)
{
  TheObstackHeapType *th = new TheObstackHeapType;
  if (th)
    {
      th->reset (chunk_size);
      return (UINTPTR) th;
    }
  return 0;
}

void
hl_clear_ostk_heap (UINTPTR heap_id)
{
  TheObstackHeapType *th = (TheObstackHeapType *) heap_id;
  if (th)
    {
      th->clear();
    }
}

void
hl_unregister_ostk_heap (UINTPTR heap_id)
{
  TheObstackHeapType *th = (TheObstackHeapType *) heap_id;
  if (th)
    {
      delete th;
    }
}

// alloc & free
void *
hl_ostk_alloc (UINTPTR heap_id, size_t sz)
{
  TheObstackHeapType *th = (TheObstackHeapType *) heap_id;
  if (th)
    {
      return th->malloc (sz);
    }
  return NULL;
}

void *
hl_ostk_realloc (UINTPTR heap_id, void *ptr, size_t sz)
{
  TheObstackHeapType *th = (TheObstackHeapType *) heap_id;
  if (th)
    {
      void *new_ptr = th->malloc (sz);
      size_t old_sz = th->getSize (ptr);

      memcpy (new_ptr, ptr, (old_sz > sz ? sz : old_sz));

      // free at a time
      // if (ptr) th->free (ptr);
      return new_ptr;
    }
  return NULL;
}

void
hl_ostk_free (UINTPTR heap_id, void *ptr)
{
  TheObstackHeapType *th = (TheObstackHeapType *) heap_id;
  if (th)
    {
      th->free (ptr);
    }
}

//
// Kingsley Heap
//

// class definition
class TopHeap : public SizeHeap<UniqueHeap<mallocHeap> > {};

class TheKingsleyHeapType :
  public ANSIWrapper<KingsleyHeap<AdaptHeap<DLList, TopHeap>, TopHeap> > {};

// initialize & finalize
UINTPTR
hl_register_kingsley_heap (/*int chunk_size*/)
{
  TheKingsleyHeapType *th = new TheKingsleyHeapType;
  if (th)
    {
      //th->reset(chunk_size);
      return (UINTPTR) th;
    }
  return 0;
}

/*
void
hl_clear_kingsley_heap (unsigned int heap_id)
{
  TheKingsleyHeapType * th = (TheKingsleyHeapType *) heap_id;
  if (th)
    {
      th->clear();
    }
}
*/

void
hl_unregister_kingsley_heap (UINTPTR heap_id)
{
  TheKingsleyHeapType *th = (TheKingsleyHeapType *) heap_id;
  if (th)
    {
      delete th;
    }
}

// alloc & free
void *
hl_kingsley_alloc (UINTPTR heap_id, size_t sz)
{
  TheKingsleyHeapType *th = (TheKingsleyHeapType *) heap_id;
  if (th)
    {
      return th->malloc (sz);
    }
  return NULL;
}

void *
hl_kingsley_realloc (UINTPTR heap_id, void *ptr, size_t sz)
{
  TheKingsleyHeapType *th = (TheKingsleyHeapType *) heap_id;
  if (th)
    {
      void *new_ptr = th->malloc (sz);
      size_t old_sz = th->getSize (ptr);

      memcpy (new_ptr, ptr, (old_sz > sz ? sz : old_sz));

      if (ptr)
        {
          th->free (ptr);
        }
      return new_ptr;
    }
  return NULL;
}

void
hl_kingsley_free (UINTPTR heap_id, void *ptr)
{
  TheKingsleyHeapType *th = (TheKingsleyHeapType *) heap_id;
  if (th)
    {
      th->free (ptr);
    }
}

