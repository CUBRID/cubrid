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
// Custom Heap Memory Allocators
//

#include <stdlib.h>
#include <new>

#include "porting_inline.hpp"
#include "system.h"
#include "heaplayers.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#undef malloc
#undef free

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

#define malloc(sz) cub_alloc(sz, __FILE__, __LINE__)
#define free(ptr) cub_free(ptr)
