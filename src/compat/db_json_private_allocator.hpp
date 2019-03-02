#ifndef _DB_JSON_PRIVATE_ALLOCATOR_
#define _DB_JSON_PRIVATE_ALLOCATOR_

#include "rapidjson/allocators.h"

class JSON_PRIVATE_ALLOCATOR
{
  public:
    static const bool kNeedFree;
    void *Malloc (size_t size);
    void *Realloc (void *originalPtr, size_t originalSize, size_t newSize);
    static void Free (void *ptr);
};

typedef rapidjson::MemoryPoolAllocator <JSON_PRIVATE_ALLOCATOR> JSON_PRIVATE_MEMPOOL;

#endif