//
// Custom Heap Memory Allocators
//

#include <stdlib.h>
#include <new>

#include "porting_inline.hpp"
#include "system.h"
#include "heaplayers.h"

using namespace HL;

volatile int anyThreadCreatedInHL = 1;

//
// Fixed Heap
//

// class definition
class TheFixedHeapType :
  public LockedHeap<SpinLockType,FreelistHeap<ZoneHeap<MallocHeap,0> > > {};

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
  public SizeHeap<ObstackHeap<0,MallocHeap> > {};

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

void
hl_ostk_free (UINTPTR heap_id, void *ptr)
{
  TheObstackHeapType *th = (TheObstackHeapType *) heap_id;
  if (th)
    {
      th->free (ptr);
    }
}

